#include "watch_settings.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"
#include "display_driver.h"
#include "i18n.h"
#include "measure.h"
#include "nvs_storage.h"
#include "power_save.h"

static const char *TAG = "watch_settings";
static volatile uint16_t s_beep_timer_s;
static volatile uint32_t s_next_beep_ms;
static TaskHandle_t s_beep_task;

/* settings_t stores the beep and sleep delays as table indices, which is what
 * NVS round-trips. These translate between the index and the seconds the phone
 * and the runtime use. */
static const uint16_t beep_timer_seconds[] = { 0, 60, 180, 300, 600 };
static const uint16_t sleep_timer_seconds[] = { 300, 600, 1800 };

#define WATCH_SETTINGS_COUNT(array) (sizeof(array) / sizeof((array)[0]))

static uint16_t index_to_seconds(const uint16_t *table, size_t count, uint8_t index)
{
    return table[(index < count) ? index : 0];
}

static bool seconds_to_index(const uint16_t *table, size_t count, int seconds, uint8_t *index)
{
    for (size_t i = 0; i < count; i++) {
        if (table[i] == (uint16_t)seconds) {
            *index = (uint8_t)i;
            return true;
        }
    }
    return false;
}

static void apply_runtime(const settings_t *settings)
{
    buzzer_set_volume_level(settings->volume);
    display_set_brightness_level(settings->brightness);

    power_save_set_timeout(index_to_seconds(sleep_timer_seconds,
                                            WATCH_SETTINGS_COUNT(sleep_timer_seconds),
                                            settings->sleep_timer));

    measure_set_unit_pma(settings->measurement_unit != 0);

    i18n_select_lang_by_code(settings->lang);
    i18n_apply_ui();

    const uint16_t beep_seconds = index_to_seconds(beep_timer_seconds,
                                                   WATCH_SETTINGS_COUNT(beep_timer_seconds),
                                                   settings->beep_timer);
    s_next_beep_ms = (uint32_t)(esp_timer_get_time() / 1000) + (uint32_t)beep_seconds * 1000U;
    s_beep_timer_s = beep_seconds;
}

static void beep_task(void *arg)
{
    (void)arg;
    while (true) {
        uint16_t seconds = s_beep_timer_s;
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (seconds != 0 && (int32_t)(now_ms - s_next_beep_ms) >= 0) {
            s_next_beep_ms = now_ms + (uint32_t)seconds * 1000U;
            playSoundOnce(1200, 150);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t watch_settings_init(void)
{
    settings_t settings;
    esp_err_t err = nvs_settings_load(&settings);
    if (err != ESP_OK) return err;
    apply_runtime(&settings);
    if (s_beep_task == NULL &&
        xTaskCreate(beep_task, "beep_timer", 2048, NULL, 2, &s_beep_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static bool valid_integer(const cJSON *value, int min, int max)
{
    return cJSON_IsNumber(value) && isfinite(value->valuedouble) &&
           value->valuedouble == (double)value->valueint &&
           value->valueint >= min && value->valueint <= max;
}

bool watch_settings_apply_json(const char *payload, size_t length)
{
    if (payload == NULL || length == 0 || length > 128) return false;
    char json[129];
    memcpy(json, payload, length);
    json[length] = '\0';
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!valid_integer(version, 1, 1) || !cJSON_IsString(type) ||
        strcmp(type->valuestring, "config") != 0 || cJSON_GetArraySize(root) != 3) {
        cJSON_Delete(root);
        return false;
    }

    settings_t next;
    if (nvs_settings_load(&next) != ESP_OK) {
        cJSON_Delete(root);
        return false;
    }

    bool valid = false;
    const cJSON *value = NULL;
    if ((value = cJSON_GetObjectItemCaseSensitive(root, "measurementUnit")) != NULL) {
        if (cJSON_IsString(value)) {
            measure_unit_t unit;
            if (measure_unit_from_name(value->valuestring, &unit)) {
                next.measurement_unit = (unit == MEASURE_UNIT_PMA) ? 1 : 0;
                valid = true;
            }
        }
    } else if ((value = cJSON_GetObjectItemCaseSensitive(root, "beepTimer")) != NULL) {
        if (valid_integer(value, 0, 600) &&
            seconds_to_index(beep_timer_seconds,
                             WATCH_SETTINGS_COUNT(beep_timer_seconds),
                             value->valueint, &next.beep_timer)) {
            next.beep_enabled = (value->valueint != 0);
            valid = true;
        }
    } else if ((value = cJSON_GetObjectItemCaseSensitive(root, "sleepTimer")) != NULL) {
        if (valid_integer(value, 300, 1800) &&
            seconds_to_index(sleep_timer_seconds,
                             WATCH_SETTINGS_COUNT(sleep_timer_seconds),
                             value->valueint, &next.sleep_timer)) {
            valid = true;
        }
    } else if ((value = cJSON_GetObjectItemCaseSensitive(root, "volume")) != NULL) {
        if (valid_integer(value, 0, BUZZER_VOLUME_LEVEL_MAX)) {
            next.volume = (uint8_t)value->valueint;
            valid = true;
        }
    } else if ((value = cJSON_GetObjectItemCaseSensitive(root, "brightness")) != NULL) {
        if (valid_integer(value, 0, DISPLAY_BRIGHTNESS_LEVEL_MAX)) {
            next.brightness = (uint8_t)value->valueint;
            valid = true;
        }
    } else if ((value = cJSON_GetObjectItemCaseSensitive(root, "langSelector")) != NULL) {
        if (cJSON_IsString(value) &&
            (strcmp(value->valuestring, "en") == 0 || strcmp(value->valuestring, "hu") == 0 ||
             strcmp(value->valuestring, "de") == 0 || strcmp(value->valuestring, "es") == 0 ||
             strcmp(value->valuestring, "fr") == 0)) {
            strcpy(next.lang, value->valuestring);
            valid = true;
        }
    }

    cJSON_Delete(root);
    if (!valid) return false;

    esp_err_t err = nvs_settings_save(&next);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not persist config: %s", esp_err_to_name(err));
        return false;
    }
    apply_runtime(&next);
    return true;
}
