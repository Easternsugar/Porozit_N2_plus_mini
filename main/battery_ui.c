/**
 * @file battery_ui.c
 * @brief Battery driven UI: status icon, charge-only mode, empty battery cutoff
 *
 * While a charger is detected the battery icon (ui_batt) is replaced by the
 * charge icon; when the cable is unplugged it goes back to the icon matching
 * the current battery level.
 *
 * The same ui_charge screen is used for two different situations: as a
 * charge-only indicator when USB powered the watch up, and as the empty battery
 * warning shown right before the watch shuts itself down.
 */

#include "battery_ui.h"
#include "battery_monitor.h"
#include "buzzer.h"
#include "power_manager.h"
#include "power_save.h"
#include "ui_screens.h"
#include "ui/ui.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "battery_ui";

#define BATTERY_UI_TASK_STACK   4096
#define BATTERY_UI_TASK_PRIO    2

/* Power button polling while in charge-only mode */
#define CHARGE_ONLY_POLL_MS     50
/* Consecutive polls the button must be down before the watch powers up */
#define CHARGE_ONLY_PRESS_POLLS 3

/* Empty battery alarm: three short beeps, then one long one on power off */
#define BATTERY_ALARM_FREQ_HZ   800
#define BATTERY_ALARM_BEEP_MS   150
#define BATTERY_ALARM_BEEPS     3
#define BATTERY_OFF_BEEP_MS     500

static const lv_img_dsc_t *battery_icon_for_percent(const int percent)
{
    if (percent >= 75) return &ui_img_img_battery_full_png;
    if (percent >= 50) return &ui_img_img_battery_75_png;
    if (percent >= 25) return &ui_img_img_battery_50_png;
    return &ui_img_img_battery_empty_png;
}

/**
 * @brief Pick the icon for the current battery state
 * @note  The charge icon is the same 34x28 size as the battery icons, so it
 *        drops straight into ui_batt without moving anything else.
 */
static const lv_img_dsc_t *battery_icon_for_status(const battery_status_t *status)
{
    if (status->charging) {
        return &ui_img_img_charge_png;
    }
    return battery_icon_for_percent(status->percentage);
}

static bool battery_is_empty(const battery_status_t *status)
{
    return !status->charging && status->voltage_mv <= BATTERY_CRITICAL_MV;
}

/**
 * @brief Show the empty battery warning, then power off
 *
 * Keeps sampling while the warning is up so that plugging in a charger cancels
 * the shutdown instead of killing the watch under the user's hands.
 *
 * @return true if the watch was powered off, false if the shutdown was cancelled
 */
static bool battery_ui_shutdown_empty(void)
{
    ESP_LOGW(TAG, "Battery empty, powering off in %d ms", BATTERY_EMPTY_SHUTDOWN_MS);

    /* The warning is useless on a dark screen, so wake up first */
    power_save_wake();
    ui_show_battery_empty_screen();
    playSound(BATTERY_ALARM_FREQ_HZ, BATTERY_ALARM_BEEP_MS, BATTERY_ALARM_BEEPS);

    const int steps = BATTERY_EMPTY_SHUTDOWN_MS / BATTERY_SAMPLE_PERIOD_MS;
    for (int i = 0; i < steps; i++) {
        /* Keep the warning visible for the whole countdown */
        power_save_notify_activity();
        vTaskDelay(pdMS_TO_TICKS(BATTERY_SAMPLE_PERIOD_MS));

        battery_status_t status;
        if (battery_monitor_update(&status) == ESP_OK && status.charging) {
            ESP_LOGI(TAG, "Charger connected, shutdown cancelled");
            return false;
        }
    }

    /* Same shutdown beep the power button gives */
    playSoundOnce(BATTERY_ALARM_FREQ_HZ, BATTERY_OFF_BEEP_MS);
    power_manager_power_off();
    return true;
}

bool battery_ui_handle_empty_battery(void)
{
    battery_status_t status;

    /* Confirm over several samples so a load transient cannot shut us down */
    for (int i = 0; i < BATTERY_CRITICAL_SAMPLES; i++) {
        if (i > 0) {
            vTaskDelay(pdMS_TO_TICKS(BATTERY_SAMPLE_PERIOD_MS));
        }
        if (battery_monitor_update(&status) != ESP_OK) {
            return false;
        }
        if (!battery_is_empty(&status)) {
            return false;
        }
    }

    return battery_ui_shutdown_empty();
}

void battery_ui_run_charge_screen(bool drive_monitor)
{
    ESP_LOGI(TAG, "Switched off on the cable, showing the charge screen only");

    /* Running with the battery rail unlatched is proof that VBUS feeds us */
    battery_monitor_seed_charging(true);

    /* Nobody is looking at a watch that is only sitting on the cable, so use
     * the short idle timeout for as long as this screen is up. */
    power_save_set_charge_only(true);

    ui_show_charge_screen();

    uint32_t pressed_polls = 0;
    uint32_t polls_since_sample = 0;
    const uint32_t polls_per_sample = BATTERY_SAMPLE_PERIOD_MS / CHARGE_ONLY_POLL_MS;

    while (1) {
        if (power_manager_is_button_pressed()) {
            if (++pressed_polls >= CHARGE_ONLY_PRESS_POLLS) {
                break;
            }
        } else {
            pressed_polls = 0;
        }

        /* Keep the charge detection warm so the dashboard shows the right icon
         * straight away once the user powers the watch up. Skipped when the
         * battery UI task is already doing it, to avoid racing on the filter. */
        if (drive_monitor && ++polls_since_sample >= polls_per_sample) {
            polls_since_sample = 0;
            battery_monitor_update(NULL);
        }

        vTaskDelay(pdMS_TO_TICKS(CHARGE_ONLY_POLL_MS));
    }

    /* Latch the battery rail, otherwise unplugging USB would kill the watch */
    power_manager_power_on();
    ESP_LOGI(TAG, "Power button pressed, continuing with a normal boot");

    /* The watch is in use from here on, back to the normal idle timeout even
     * though the cable is still connected. */
    power_save_set_charge_only(false);

    /* The power button is not a touch event, so the idle watchdog has not seen
     * it. Bring the screen back before the caller carries on. */
    power_save_wake();

    /* Deliberately no wait for the button to be released: a normal power-on
     * starts the moment the press is recognised, and this has to feel the same.
     * Keeping the button down from here just counts towards the usual
     * hold-to-power-off, exactly like it would on a running watch. */
}

static void battery_ui_task(void *param)
{
    (void)param;

    const lv_img_dsc_t *shown_icon = NULL;
    int shown_charging = -1;
    int empty_streak = 0;

    while (1) {
        battery_status_t status;

        if (battery_monitor_update(&status) == ESP_OK) {
            const lv_img_dsc_t *icon = battery_icon_for_status(&status);

            /* Plugging the cable in or out is physical interaction: light the
             * screen up so the user gets feedback. While charging the idle
             * timeout is short, so it goes dark again on its own. */
            if ((int)status.charging != shown_charging) {
                shown_charging = (int)status.charging;
                power_save_notify_activity();
            }

            if (icon != shown_icon) {
                lvgl_port_lock(-1);
                if (ui_batt != NULL) {
                    lv_img_set_src(ui_batt, icon);
                    shown_icon = icon;
                }
                lvgl_port_unlock();
            }

            if (battery_is_empty(&status)) {
                if (++empty_streak >= BATTERY_CRITICAL_SAMPLES) {
                    empty_streak = 0;
                    if (battery_ui_shutdown_empty()) {
                        /* Rail is going down, nothing left to do */
                        while (1) {
                            vTaskDelay(portMAX_DELAY);
                        }
                    }
                    /* Cancelled by the charger, go back to the dashboard */
                    ui_show_dashboard_screen();
                    shown_icon = NULL;
                }
            } else {
                empty_streak = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BATTERY_SAMPLE_PERIOD_MS));
    }
}

void battery_ui_start(void)
{
    ESP_LOGI(TAG, "Starting battery indicator");
    xTaskCreate(battery_ui_task, "bat_ui", BATTERY_UI_TASK_STACK, NULL, BATTERY_UI_TASK_PRIO, NULL);
}
