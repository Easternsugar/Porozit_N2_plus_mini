/**
 * @file main.c
 * @brief Main application entry point
 * 
 * This application initializes:
 * - Power management (with 2-second hold-to-power-off feature)
 * - LCD display (ST7789)
 * - Touch input (CST816S)
 * - Battery monitoring
 * - LVGL graphics library with SquareLine Studio UI
 * - BLE notify
 * - NVS storage
 */

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <string.h>
#include "ui/ui.h"
#include "nvs_flash.h"

/* Application modules */
#include "power_manager.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "battery_monitor.h"
#include "battery_ui.h"
#include "buzzer.h"
#include "i18n.h"
#include "power_save.h"
#include "watch_settings.h"
#include "measure.h"
#include "ble_service.h"
#include "ble_protocol.h"
#include "nvs_storage.h"
#include "ui_screens.h"

/* Enable/disable touch functionality */
#define USE_TOUCH 1

static const char *TAG = "main";
// static const char *HEARTBEAT_PAYLOAD = "heartbeat";

/**
 * @brief Whether this boot came from power being applied, not from a reset
 *
 * Flashing or debugging over USB restarts the chip through the USB peripheral.
 * Those resets look exactly like a charger powering the watch up (the power
 * button is not pressed), so they are filtered out here to keep the normal
 * development workflow intact.
 */
static bool app_main_is_cold_boot(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    return reason == ESP_RST_POWERON || reason == ESP_RST_UNKNOWN;
}

/**
 * @brief Initialize and display the UI
 */
static void app_main_display(void)
{
    /* Lock LVGL mutex */
    lvgl_port_lock(-1);

    /* Initialize SquareLine Studio generated UI */
    ui_init();

    /* ui_overlay is the full screen dimmer that belongs to the slide-up menu.
     * SquareLine creates it visible, and every lv_obj_create() object is
     * clickable by default, so it covers the whole dashboard and swallows all
     * taps. Only the menu-close gesture ever sets its hidden flag, which is why
     * the buttons used to come alive only after opening and closing the menu.
     * The generated code hides it too, but repeating it here keeps the fix in
     * place if the UI is re-exported from SquareLine. */
    if (ui_overlay != NULL) {
        lv_obj_add_flag(ui_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    /* Unlock LVGL mutex */
    lvgl_port_unlock();

    /* Replace the generated swipe handler with the state driven one, otherwise
     * repeated swipes push the settings panel past its end positions. */
    ui_menu_init();
}

static void ble_heartbeat_task(void *param)
{
    (void)param;

    while (1) {
        /* Protocol v2 "battery" message; it reuses the battery UI task's sample
         * instead of hitting the ADC from two tasks. */
        if (ble_service_can_notify()) {
            ble_protocol_send_battery();
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Application Starting ===");

    esp_err_t ret = nvs_flash_init();
    if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_LOGI(TAG, "Initializing NVS...");
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Free Heap: %u bytes", xPortGetFreeHeapSize());

    /* Step 1: Initialize power management first */
    ESP_LOGI(TAG, "Initializing power management...");
    ESP_ERROR_CHECK(power_manager_init());

    ESP_LOGI(TAG, "Free Heap: %u bytes", xPortGetFreeHeapSize());

    /* Step 2: Initialize battery monitoring */
    ESP_LOGI(TAG, "Initializing battery monitor...");
    if (battery_monitor_init() == ESP_OK) {
        int voltage_mv;
        if (battery_monitor_read_voltage(&voltage_mv) == ESP_OK) {
            ESP_LOGI(TAG, "Battery voltage: %.2f V", voltage_mv / 1000.0f);
        }
    }

    ESP_LOGI(TAG, "Free Heap: %u bytes", xPortGetFreeHeapSize());

    /* Step 2b: Set up the buzzer PWM once, so no beep has to configure it */
    ESP_LOGI(TAG, "Initializing buzzer...");
    buzzer_init();

    /* Step 3: Initialize LCD display */
    ESP_LOGI(TAG, "Initializing LCD display...");
    ESP_ERROR_CHECK(display_lcd_init());

    ESP_LOGI(TAG, "Free Heap: %u bytes", xPortGetFreeHeapSize());

#if USE_TOUCH
    /* Step 4: Initialize touch driver */
    ESP_LOGI(TAG, "Initializing touch driver...");
    ESP_ERROR_CHECK(touch_driver_init(LCD_H_RES, LCD_V_RES));

    ESP_LOGI(TAG, "Free Heap: %u bytes", xPortGetFreeHeapSize());
#endif

    /* Step 5: Initialize LVGL */
    ESP_LOGI(TAG, "Initializing LVGL...");
    ESP_ERROR_CHECK(display_lvgl_init());

    ESP_LOGI(TAG, "Free Heap: %u bytes", xPortGetFreeHeapSize());

#if USE_TOUCH
    /* Step 6: Register touch with LVGL.
     * The LVGL port task is already running at this point, so the mutex is
     * mandatory: lv_indev_drv_register() edits LVGL's input device list and
     * creates a timer, which must not race with lv_timer_handler(). */
    lvgl_port_lock(-1);
    esp_err_t touch_ret = touch_driver_register_lvgl(display_get_lvgl_display());
    lvgl_port_unlock();
    ESP_ERROR_CHECK(touch_ret);
#endif

    /* Step 7: Initialize UI */
    ESP_LOGI(TAG, "Initializing UI...");
    app_main_display();

    /* The language has to be selected before anything draws text. The charge
     * and empty battery screens further down can be the first thing the user
     * ever sees on this boot, and they are localized as well. */
    char boot_lang[SETTINGS_LANG_SIZE];
    nvs_settings_get_lang(boot_lang, sizeof(boot_lang));
    i18n_select_lang_by_code(boot_lang);
    ESP_LOGI(TAG, "Language: %s", i18n_lang_code(i18n_get_lang()));

    /* Show the loading/splash screen while the rest of the boot finishes */
    ui_show_load_screen();

    /* Start the idle watchdog now so charge-only mode also dims the screen */
    ESP_ERROR_CHECK(power_save_start());

    /* Step 7a: The watch also boots when USB is plugged into a powered-off
     * watch, because VBUS feeds the 3V3 regulator directly. In that case only
     * show the charge screen and wait for the user to press the power button.
     * A USB triggered reset (flashing, JTAG) is excluded so that the normal
     * development workflow is not affected. */
    if (!power_manager_booted_from_button() && app_main_is_cold_boot()) {
        /* Nothing else samples the battery yet, so this loop has to */
        battery_ui_run_charge_screen(true);
    }

    /* Step 7b: Refuse to boot on an empty battery: warn, then power off */
    if (battery_ui_handle_empty_battery()) {
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /* Both of the above take over the display, put the splash back */
    ui_show_load_screen();

    ESP_LOGI(TAG, "Free Heap: %u bytes", xPortGetFreeHeapSize());

    /* Step 7c: Initialize measurement logic */
    ESP_LOGI(TAG, "Initializing measurement...");
    ESP_ERROR_CHECK(measure_init());

    ESP_LOGI(TAG, "Free Heap: %u bytes", xPortGetFreeHeapSize());

    /* Step 7d: Restore saved settings from NVS */
    ESP_LOGI(TAG, "Restoring settings from NVS...");
    settings_t settings;
    esp_err_t settings_ret = nvs_settings_load(&settings);
    if(settings_ret == ESP_OK){
        /* The device-only beep on/off switch. Everything the phone can change
         * (timeouts, levels, unit, language) is applied by watch_settings_init(). */
        buzzer_set_enabled(settings.beep_enabled);

        ESP_LOGI(TAG, "Settings restored: beep=%d, lang=%s, beepTimer=%u, "
                      "sleepTimer=%u, brightness=%u, volume=%u",
                 settings.beep_enabled, settings.lang,
                 (unsigned)settings.beep_timer, (unsigned)settings.sleep_timer,
                 (unsigned)settings.brightness, (unsigned)settings.volume);
    } else {
        ESP_LOGW(TAG, "Could not read settings (%s), keeping defaults",
                 esp_err_to_name(settings_ret));
    }

    /* Write the restored language and beep state into every label. This also
     * moves the labels SquareLine left on the accent-less default font onto a
     * font that has the accented glyphs. */
    i18n_apply_ui();

    /* Restore the settings shared with the Android screen before BLE starts. */
    ESP_ERROR_CHECK(watch_settings_init());

    /* Step 8: start the battery / charge indicator task */
    ESP_LOGI(TAG, "Starting battery UI...");
    battery_ui_start();

    /* Step 8: Initialize BLE */
    ESP_LOGI(TAG, "Initializing BLE...");
    ESP_ERROR_CHECK(ble_service_init());

    /* Handles the JSON messages the phone writes to the characteristic */
    ESP_ERROR_CHECK(ble_protocol_init());

    ESP_LOGI(TAG, "Free Heap: %u bytes", xPortGetFreeHeapSize());

    xTaskCreate(ble_heartbeat_task, "ble_heartbeat", 4096, NULL, 3, NULL);

    /* Step 9: Start power button monitoring (background task) */
    ESP_LOGI(TAG, "Starting power button monitoring...");
    ESP_ERROR_CHECK(power_manager_start_monitoring());

    ESP_LOGI(TAG, "Free Heap: %u bytes", xPortGetFreeHeapSize());

    ESP_LOGI(TAG, "=== Application Started Successfully ===");
    ESP_LOGI(TAG, "Hold power button for 2 seconds to power off");

    ESP_LOGI(TAG, "Free Heap: %u bytes", xPortGetFreeHeapSize());

    playSound(1200, 300, 2);
    ESP_LOGI(TAG, "sound started...");

    /* Boot finished: keep the splash visible briefly, then show the dashboard */
    vTaskDelay(pdMS_TO_TICKS(1500));
    ui_show_dashboard_screen();

    /* Keep app_main alive to avoid returning the main task. */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
