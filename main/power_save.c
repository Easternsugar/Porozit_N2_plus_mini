/**
 * @file power_save.c
 * @brief Idle sleep implementation
 *
 * Only the power save task ever changes the sleep state. Everything else just
 * writes a timestamp, which keeps the notification path lock free and avoids
 * any lock ordering problems with the LVGL mutex.
 */

#include "power_save.h"
#include "ble_service.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power_save";

/* Touch is polled at this rate while asleep, so it also bounds the wake up
 * latency. A short tap must not fall between two polls. */
#define POWER_SAVE_POLL_MS      50
/* Upper bound for waiting out the tap that woke the watch up */
#define POWER_SAVE_RELEASE_MS   1000
/* Upper bound for power_save_wake() waiting on the power save task */
#define POWER_SAVE_WAKE_WAIT_MS 500

static TaskHandle_t s_task = NULL;
/* 32 bit loads and stores are atomic, no lock needed for these */
static volatile uint32_t s_last_activity_ms = 0;
static volatile bool s_wake_request = false;
static volatile bool s_sleeping = false;
static volatile bool s_charge_only = false;
/* Runtime adjustable, the phone can change it over BLE */
static volatile uint32_t s_timeout_ms = POWER_SAVE_TIMEOUT_MS;

static uint32_t power_save_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief Idle timeout for the current mode
 * @note  Deliberately keyed on charge-only mode and not on "is the charger
 *        connected". Plugging a switched-on watch in must not shorten the
 *        timeout, the user is still holding it.
 */
static uint32_t power_save_timeout_ms(void)
{
    return s_charge_only ? POWER_SAVE_TIMEOUT_CHARGE_ONLY_MS
                         : s_timeout_ms;
}

void power_save_set_timeout_ms(uint32_t timeout_ms)
{
    s_timeout_ms = (timeout_ms > 0) ? timeout_ms : POWER_SAVE_TIMEOUT_MS;

    /* Give the new value a full window instead of measuring from the old idle */
    s_last_activity_ms = power_save_now_ms();

    ESP_LOGI(TAG, "Idle timeout set to %u ms", (unsigned)s_timeout_ms);
}

/**
 * @brief Read the touch controller directly
 * @note  Only safe while LVGL is stopped, otherwise it races with the LVGL
 *        input device callback.
 */
static bool power_save_touch_pressed(void)
{
    esp_lcd_touch_handle_t tp = touch_driver_get_handle();
    if (tp == NULL) {
        return false;
    }

    if (esp_lcd_touch_read_data(tp) != ESP_OK) {
        return false;
    }

    esp_lcd_touch_point_data_t point = { 0 };
    uint8_t count = 0;
    if (esp_lcd_touch_get_data(tp, &point, &count, 1) != ESP_OK) {
        return false;
    }

    return count > 0;
}

static void power_save_enter(void)
{
    ESP_LOGI(TAG, "Going to sleep");

    display_set_backlight(false);

    /* Taking the lock first makes sure the LVGL task is not inside
     * lv_timer_handler(), so no flush can start after the timers are off. */
    lvgl_port_lock(-1);
    lvgl_port_stop();
    lvgl_port_unlock();

    /* Let any in flight SPI flush finish before talking to the panel */
    vTaskDelay(pdMS_TO_TICKS(POWER_SAVE_POLL_MS));

    esp_lcd_panel_handle_t panel = display_get_lcd_panel();
    if (panel != NULL) {
        esp_lcd_panel_disp_on_off(panel, false);
    }

    ble_service_set_low_power(true);

    s_sleeping = true;
}

static void power_save_exit(void)
{
    ESP_LOGI(TAG, "Waking up");

    /* Wait out the tap that woke us so it does not reach the UI as a click */
    for (int i = 0; i < POWER_SAVE_RELEASE_MS / POWER_SAVE_POLL_MS; i++) {
        if (!power_save_touch_pressed()) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(POWER_SAVE_POLL_MS));
    }

    esp_lcd_panel_handle_t panel = display_get_lcd_panel();
    if (panel != NULL) {
        esp_lcd_panel_disp_on_off(panel, true);
    }

    lvgl_port_lock(-1);
    lvgl_port_resume();
    lv_obj_t *screen = lv_scr_act();
    if (screen != NULL) {
        lv_obj_invalidate(screen);
    }
    lvgl_port_unlock();

    display_set_backlight(true);

    ble_service_set_low_power(false);

    s_last_activity_ms = power_save_now_ms();
    s_sleeping = false;
}

static void power_save_task(void *param)
{
    (void)param;

    ESP_LOGI(TAG, "Idle watchdog started (%d ms, %d ms in charge-only mode)",
             POWER_SAVE_TIMEOUT_MS, POWER_SAVE_TIMEOUT_CHARGE_ONLY_MS);

    while (1) {
        if (s_sleeping) {
            /* Either somebody reported activity or the screen was touched */
            if (s_wake_request || power_save_touch_pressed()) {
                s_wake_request = false;
                power_save_exit();
            }
        } else {
            /* Nothing to wake up from, keep the flag from going stale */
            s_wake_request = false;

            if (power_save_now_ms() - s_last_activity_ms >= power_save_timeout_ms()) {
                power_save_enter();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POWER_SAVE_POLL_MS));
    }
}

esp_err_t power_save_start(void)
{
    if (s_task != NULL) {
        return ESP_OK;
    }

    s_last_activity_ms = power_save_now_ms();
    s_sleeping = false;

    if (xTaskCreate(power_save_task, "power_save", 3072, NULL, 3, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create power save task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void power_save_notify_activity(void)
{
    s_last_activity_ms = power_save_now_ms();
    s_wake_request = true;
}

void power_save_set_charge_only(bool charge_only)
{
    if (s_charge_only == charge_only) {
        return;
    }

    s_charge_only = charge_only;
    /* Give the new mode a full timeout instead of inheriting the old idle time */
    s_last_activity_ms = power_save_now_ms();

    ESP_LOGI(TAG, "Idle timeout is now %u ms (%s)",
             (unsigned)power_save_timeout_ms(),
             charge_only ? "charge only" : "normal");
}

void power_save_set_timeout(uint16_t seconds)
{
    /* Seconds based front end for the same setting, used by watch_settings.c.
     * Only the three values the phone offers are accepted. */
    if (seconds != 300 && seconds != 600 && seconds != 1800) {
        ESP_LOGW(TAG, "Ignoring an unsupported idle timeout of %u s",
                 (unsigned)seconds);
        return;
    }

    power_save_set_timeout_ms((uint32_t)seconds * 1000U);
}

void power_save_wake(void)
{
    power_save_notify_activity();

    for (int i = 0; i < POWER_SAVE_WAKE_WAIT_MS / POWER_SAVE_POLL_MS; i++) {
        if (!s_sleeping) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(POWER_SAVE_POLL_MS));
    }
}

bool power_save_is_sleeping(void)
{
    return s_sleeping;
}
