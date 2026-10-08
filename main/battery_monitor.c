/**
 * @file battery_monitor.c
 * @brief Battery voltage monitoring and charge detection implementation
 */

#include "battery_monitor.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#if BATTERY_CHARGE_GPIO >= 0
#include "driver/gpio.h"
#endif

static const char *TAG = "battery_monitor";

/* ADC handles */
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t cali_handle = NULL;
static bool is_calibrated = false;

/* adc_oneshot_read() only *tries* to take the unit lock and fails with
 * ESP_ERR_TIMEOUT if another task is reading, so serialize all access here. */
static SemaphoreHandle_t adc_mutex = NULL;

/* Published state + the filter/detector state that produces it */
static SemaphoreHandle_t state_mutex = NULL;
static battery_status_t s_status = { .voltage_mv = 0, .percentage = 0, .charging = false, .valid = false };

static int      s_filtered_mv = 0;                          /* EMA of the raw samples */
static int      s_step_hist[BATTERY_CHG_STEP_SAMPLES];      /* short window, one slot per sample */
static uint32_t s_sample_count = 0;
static int      s_trend_hist[BATTERY_CHG_TREND_SLOTS];      /* long window, decimated */
static uint32_t s_trend_count = 0;
static int      s_rise_streak = 0;
static int      s_fall_streak = 0;
static bool     s_charging = false;
static bool     s_charging_seeded = false;

/**
 * @brief Average `samples` ADC conversions and convert to battery millivolts
 * @param samples  Number of conversions to average
 * @param delay_ms Delay between conversions (0 = back to back)
 */
static esp_err_t battery_sample_mv(int samples, int delay_ms, int *voltage_mv)
{
    if (adc_handle == NULL) {
        ESP_LOGE(TAG, "Battery monitor not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (voltage_mv == NULL || samples <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(adc_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "ADC busy");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    int raw_sum = 0;
    int taken = 0;

    for (int i = 0; i < samples; i++) {
        int raw;
        ret = adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(ret));
            break;
        }
        raw_sum += raw;
        taken++;
        if (delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }

    if (ret == ESP_OK && taken > 0) {
        int raw_avg = raw_sum / taken;

        int adc_voltage_mv = 0;
        if (is_calibrated && cali_handle != NULL) {
            adc_cali_raw_to_voltage(cali_handle, raw_avg, &adc_voltage_mv);
        } else {
            /* Simple conversion without calibration (3.3V reference, 12-bit) */
            adc_voltage_mv = (raw_avg * 3300) / 4095;
        }

        *voltage_mv = (int)(adc_voltage_mv * BATTERY_VOLTAGE_DIVIDER_RATIO);

        ESP_LOGD(TAG, "Raw ADC (avg): %d, ADC voltage: %d mV, Battery voltage: %d mV",
                 raw_avg, adc_voltage_mv, *voltage_mv);
    }

    xSemaphoreGive(adc_mutex);
    return ret;
}

static int percentage_from_mv(int voltage_mv)
{
    if (voltage_mv <= BATTERY_EMPTY_MV) {
        return 0;
    }
    if (voltage_mv >= BATTERY_FULL_MV) {
        return 100;
    }
    return (voltage_mv - BATTERY_EMPTY_MV) * 100 / (BATTERY_FULL_MV - BATTERY_EMPTY_MV);
}

#if BATTERY_CHARGE_GPIO >= 0
/**
 * @brief Read the charger status line (exact, requires the hardware mod)
 */
static bool charger_gpio_present(void)
{
    int level = gpio_get_level((gpio_num_t)BATTERY_CHARGE_GPIO);
#if BATTERY_CHARGE_GPIO_ACTIVE_HIGH
    return level != 0;
#else
    return level == 0;
#endif
}
#else
/**
 * @brief Infer the charger state from the filtered battery voltage.
 *
 * Two independent signals are used:
 *  - a step of at least BATTERY_CHG_STEP_MV over the last 3 s, which is what a
 *    plug or unplug event looks like (load leaves the cell + charge current),
 *    confirmed over BATTERY_CHG_CONFIRM_SAMPLES samples so that a buzzer beep
 *    or a BLE burst cannot flip the state;
 *  - a slow rise/fall over the last 2 minutes, which recovers the state when
 *    the watch was booted with the cable already plugged in.
 */
static bool charger_infer(int filtered_mv)
{
    /* --- short window (plug / unplug step) --- */
    int step_slot = s_sample_count % BATTERY_CHG_STEP_SAMPLES;
    bool step_valid = s_sample_count >= BATTERY_CHG_STEP_SAMPLES;
    int step_mv = step_valid ? (filtered_mv - s_step_hist[step_slot]) : 0;
    s_step_hist[step_slot] = filtered_mv;

    /* --- long window (booted while charging) --- */
    bool trend_valid = false;
    int trend_mv = 0;
    if (s_sample_count % BATTERY_CHG_TREND_DECIM == 0) {
        int trend_slot = s_trend_count % BATTERY_CHG_TREND_SLOTS;
        trend_valid = s_trend_count >= BATTERY_CHG_TREND_SLOTS;
        trend_mv = trend_valid ? (filtered_mv - s_trend_hist[trend_slot]) : 0;
        s_trend_hist[trend_slot] = filtered_mv;
        s_trend_count++;
    }

    s_sample_count++;

    /* No history yet: unless the caller told us the answer, guess from the
     * absolute voltage. */
    if (!s_charging_seeded) {
        s_charging_seeded = true;
        return filtered_mv >= BATTERY_CHG_SEED_MV;
    }

    if (step_valid) {
        if (step_mv >= BATTERY_CHG_STEP_MV) {
            s_rise_streak++;
            s_fall_streak = 0;
        } else if (step_mv <= -BATTERY_CHG_STEP_MV) {
            s_fall_streak++;
            s_rise_streak = 0;
        } else {
            s_rise_streak = 0;
            s_fall_streak = 0;
        }

        if (s_rise_streak >= BATTERY_CHG_CONFIRM_SAMPLES) {
            return true;
        }
        if (s_fall_streak >= BATTERY_CHG_CONFIRM_SAMPLES) {
            return false;
        }
    }

    if (trend_valid) {
        if (trend_mv >= BATTERY_CHG_TREND_MV) {
            return true;
        }
        if (trend_mv <= -BATTERY_CHG_TREND_MV) {
            return false;
        }
    }

    return s_charging;
}
#endif /* BATTERY_CHARGE_GPIO */

esp_err_t battery_monitor_init(void)
{
    ESP_LOGI(TAG, "Initializing battery monitor");

    if (adc_mutex == NULL) {
        adc_mutex = xSemaphoreCreateMutex();
    }
    if (state_mutex == NULL) {
        state_mutex = xSemaphoreCreateMutex();
    }
    if (adc_mutex == NULL || state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return ESP_ERR_NO_MEM;
    }

    /* Initialize ADC unit */
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = BATTERY_ADC_UNIT,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit");
        return ret;
    }

    /* Configure ADC channel */
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = BATTERY_ADC_ATTEN,
    };
    ret = adc_oneshot_config_channel(adc_handle, BATTERY_ADC_CHANNEL, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel");
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        return ret;
    }

    /* Initialize ADC calibration */
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = BATTERY_ADC_UNIT,
        .chan = BATTERY_ADC_CHANNEL,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);
    if (ret == ESP_OK) {
        is_calibrated = true;
        ESP_LOGI(TAG, "ADC calibration enabled");
    } else {
        is_calibrated = false;
        ESP_LOGW(TAG, "ADC calibration not available, using raw values");
    }

#if BATTERY_CHARGE_GPIO >= 0
    gpio_config_t chg_io = {
        .pin_bit_mask = (1ULL << BATTERY_CHARGE_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = BATTERY_CHARGE_GPIO_ACTIVE_HIGH ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&chg_io));
    ESP_LOGI(TAG, "Charge detection: GPIO%d", BATTERY_CHARGE_GPIO);
#else
    ESP_LOGI(TAG, "Charge detection: inferred from battery voltage");
#endif

    ESP_LOGI(TAG, "Battery monitor initialized");
    return ESP_OK;
}

esp_err_t battery_monitor_read_voltage(int *voltage_mv)
{
    return battery_sample_mv(BATTERY_ADC_SAMPLES, 10, voltage_mv);
}

esp_err_t battery_monitor_get_percentage(int *percentage)
{
    if (percentage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int voltage_mv;
    esp_err_t ret = battery_monitor_read_voltage(&voltage_mv);
    if (ret != ESP_OK) {
        return ret;
    }

    *percentage = percentage_from_mv(voltage_mv);

    ESP_LOGD(TAG, "Battery: %d%% (%d mV)", *percentage, voltage_mv);
    return ret;
}

esp_err_t battery_monitor_update(battery_status_t *status)
{
    int raw_mv;
    esp_err_t ret = battery_sample_mv(BATTERY_ADC_FAST_SAMPLES, 0, &raw_mv);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Low pass the samples so the step detector works on a stable signal */
    if (!s_status.valid) {
        s_filtered_mv = raw_mv;
    } else {
        s_filtered_mv = (s_filtered_mv + raw_mv) / 2;
    }

#if BATTERY_CHARGE_GPIO >= 0
    bool charging = charger_gpio_present();
#else
    bool charging = charger_infer(s_filtered_mv);
#endif

    bool changed = (charging != s_charging) || !s_status.valid;
    s_charging = charging;

    xSemaphoreTake(state_mutex, portMAX_DELAY);
    s_status.voltage_mv = s_filtered_mv;
    s_status.percentage = percentage_from_mv(s_filtered_mv);
    s_status.charging = charging;
    s_status.valid = true;
    battery_status_t snapshot = s_status;
    xSemaphoreGive(state_mutex);

    if (changed) {
        ESP_LOGI(TAG, "Charger %s (%d mV, %d%%)",
                 charging ? "connected" : "disconnected",
                 snapshot.voltage_mv, snapshot.percentage);
    } else {
        ESP_LOGD(TAG, "Battery %d mV, %d%%, charging=%d",
                 snapshot.voltage_mv, snapshot.percentage, (int)charging);
    }

    if (status != NULL) {
        *status = snapshot;
    }
    return ESP_OK;
}

void battery_monitor_seed_charging(bool charging)
{
    s_charging = charging;
    s_charging_seeded = true;

    if (state_mutex != NULL) {
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        s_status.charging = charging;
        xSemaphoreGive(state_mutex);
    }

    ESP_LOGI(TAG, "Charge state seeded to %s", charging ? "charging" : "discharging");
}

void battery_monitor_get_status(battery_status_t *status)
{
    if (status == NULL) {
        return;
    }

    if (state_mutex == NULL) {
        status->voltage_mv = 0;
        status->percentage = 0;
        status->charging = false;
        status->valid = false;
        return;
    }

    xSemaphoreTake(state_mutex, portMAX_DELAY);
    *status = s_status;
    xSemaphoreGive(state_mutex);
}

bool battery_monitor_is_charging(void)
{
    battery_status_t status;
    battery_monitor_get_status(&status);
    return status.valid && status.charging;
}

void battery_monitor_deinit(void)
{
    if (cali_handle != NULL) {
        adc_cali_delete_scheme_curve_fitting(cali_handle);
        cali_handle = NULL;
    }

    if (adc_handle != NULL) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
    }

    is_calibrated = false;
    ESP_LOGI(TAG, "Battery monitor deinitialized");
}
