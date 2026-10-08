/**
 * @file power_manager.c
 * @brief Power management module implementation
 */

#include "power_manager.h"
#include "battery_ui.h"
#include "buzzer.h"
#include "i18n.h"
#include "power_save.h"
#include "ui_screens.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power_manager";

/* Task handle for power monitoring */
static TaskHandle_t power_monitor_task_handle = NULL;

/* Latched in power_manager_init(): true when the user started the watch */
static bool booted_from_button = false;

/**
 * @brief Power button monitoring task
 * @param pvParameters Task parameters (unused)
 */
static void power_monitor_task(void *pvParameters)
{
    uint32_t press_start_time = 0;
    bool was_pressed = false;

    ESP_LOGI(TAG, "Power monitoring task started");

    while (1) {
        bool is_pressed = power_manager_is_button_pressed();

        if (is_pressed) {
            /* Holding the button counts as interaction, do not fall asleep */
            power_save_notify_activity();
        }

        if (is_pressed && !was_pressed) {
            /* Button just pressed - record start time */
            press_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            ESP_LOGD(TAG, "Power button pressed");
            // playSound(1200, 300);
        } 
        else if (is_pressed && was_pressed) {
            /* Button still held - check duration */
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            uint32_t hold_duration = current_time - press_start_time;

            if (hold_duration >= POWER_OFF_HOLD_TIME_MS) {
                ESP_LOGI(TAG, "Power button held for %lu ms - shutting down", hold_duration);
                /* Show the goodbye screen as a shutdown indicator */
                ui_show_bye_screen(i18n(STR_BYE));
                playSoundOnce(800, 500);
                vTaskDelay(pdMS_TO_TICKS(1000));
                power_manager_power_off();

                /* Dropping PWR_EN only disconnects the battery. If the USB cable
                 * is in, VBUS keeps feeding the 3V3 regulator through D4 and the
                 * chip carries on running, so we are still here. On battery the
                 * rail collapses in well under a millisecond and this delay
                 * never returns. Reaching the next line therefore *proves* the
                 * charger is connected, no need to ask the charge detector. */
                vTaskDelay(pdMS_TO_TICKS(POWER_OFF_SETTLE_MS));

                ESP_LOGI(TAG, "Still powered, charger must be connected");

                /* Let go of the button first, otherwise this very press would
                 * be read as "power on again". */
                while (power_manager_is_button_pressed()) {
                    vTaskDelay(pdMS_TO_TICKS(POWER_BTN_POLL_MS));
                }

                /* The battery UI task keeps sampling, so do not sample here */
                battery_ui_run_charge_screen(false);

                /* Powered back on: the application is still initialized, all it
                 * needs is the dashboard back. */
                ui_show_dashboard_screen();

                press_start_time = 0;
                was_pressed = false;
                continue;
            }
        }
        else if (!is_pressed && was_pressed) {
            /* Button released */
            ESP_LOGD(TAG, "Power button released");
        }

        was_pressed = is_pressed;

        vTaskDelay(pdMS_TO_TICKS(POWER_BTN_POLL_MS));
    }
}

esp_err_t power_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing power manager");

    /* Configure power button GPIO as input with pull-up */
    gpio_config_t pwr_btn_conf = {
        .pin_bit_mask = (1ULL << PWR_BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&pwr_btn_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure power button GPIO");
        return ret;
    }

    /* Configure power enable GPIO as output */
    gpio_config_t pwr_en_conf = {
        .pin_bit_mask = (1ULL << PWR_EN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ret = gpio_config(&pwr_en_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure power enable GPIO");
        return ret;
    }

    /* Enable power - set PWR_EN opposite of button state (button is active low).
     * A pressed button means the user started the watch, so the battery rail is
     * latched. Otherwise the watch was woken by USB power and stays unlatched. */
    bool btn_level = gpio_get_level(PWR_BTN_GPIO);
    booted_from_button = (btn_level == 0);
    gpio_set_level(PWR_EN_GPIO, !btn_level);

    ESP_LOGI(TAG, "Power button level: %d, Power enable set to: %d (booted from %s)",
             btn_level, !btn_level, booted_from_button ? "button" : "USB power");

    return ESP_OK;
}

esp_err_t power_manager_start_monitoring(void)
{
    if (power_monitor_task_handle != NULL) {
        ESP_LOGW(TAG, "Power monitoring task already running");
        return ESP_OK;
    }

    BaseType_t task_created = xTaskCreate(
        power_monitor_task,
        "power_monitor",
        4096,               /* Stack size */
        NULL,               /* Parameters */
        5,                  /* Priority (higher than most tasks) */
        &power_monitor_task_handle
    );

    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create power monitoring task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Power monitoring task created");
    return ESP_OK;
}

void power_manager_power_on(void)
{
    ESP_LOGI(TAG, "Latching battery power");
    gpio_set_level(PWR_EN_GPIO, 1);
}

void power_manager_power_off(void)
{
    ESP_LOGI(TAG, "Powering off...");
    gpio_set_level(PWR_EN_GPIO, 0);
}

bool power_manager_booted_from_button(void)
{
    return booted_from_button;
}

bool power_manager_is_button_pressed(void)
{
    /* Button is active low (pressed = 0) */
    return gpio_get_level(PWR_BTN_GPIO) == 0;
}
