/**
 * @file watch_settings.c
 * @brief Settings shared with the Porozit app (PROTOCOL.md, "config")
 *
 * The single place where the phone's settings are validated, stored and
 * applied. NVS keeps the beep and sleep delays as table indices and volume /
 * brightness as native levels; the protocol speaks seconds and percent, and
 * the conversions live here.
 */

#include "watch_settings.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

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

static const uint16_t beep_timer_seconds[] = { 0, 60, 180, 300, 600 };
static const uint16_t sleep_timer_seconds[] = { 300, 600, 1800 };

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

static uint16_t index_to_seconds(const uint16_t *table, size_t count, uint8_t index)
{
    return table[(index < count) ? index : 0];
}

static bool seconds_to_index(const uint16_t *table, size_t count, int seconds, uint8_t *index)
{
    for (size_t i = 0; i < count; i++) {
        if (table[i] == seconds) {
            *index = (uint8_t)i;
            return true;
        }
    }
    return false;
}

/* 0-100 % <-> native level 0..max, rounded to the nearest level */
static uint8_t percent_to_level(int percent, uint8_t max)
{
    return (uint8_t)((percent * max + 50) / 100);
}

static int level_to_percent(uint8_t level, uint8_t max)
{
    return max == 0 ? 0 : (level * 100 + max / 2) / max;
}

static void apply_runtime(const settings_t *settings)
{
    buzzer_set_volume_level(settings->volume);
    display_set_brightness_level(settings->brightness);

    power_save_set_timeout(index_to_seconds(sleep_timer_seconds, COUNT_OF(sleep_timer_seconds),
                                            settings->sleep_timer));

    measure_set_unit_pma(settings->measurement_unit != 0);

    i18n_select_lang_by_code(settings->lang);
    i18n_apply_ui();

    const uint16_t beep_seconds = index_to_seconds(beep_timer_seconds, COUNT_OF(beep_timer_seconds),
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

static bool as_whole_number(const cJSON *value, int *out)
{
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
        value->valuedouble != floor(value->valuedouble)) {
        return false;
    }
    *out = (int)value->valuedouble;
    return true;
}

bool watch_settings_apply(const cJSON *root, const char **error)
{
    static const char *const languages[] = { "en", "hu", "de", "es", "fr" };

    settings_t next;
    if (nvs_settings_load(&next) != ESP_OK) {
        *error = "settings storage unavailable";
        return false;
    }

    /* Validate every key into a copy first: all or nothing. */
    unsigned changed = 0;
    const cJSON *value;
    int number;

    if ((value = cJSON_GetObjectItemCaseSensitive(root, "unit")) != NULL) {
        measure_unit_t unit;
        if (!cJSON_IsString(value) || !measure_unit_from_name(value->valuestring, &unit)) {
            *error = "unit: expected \"sec\" or \"pma\"";
            return false;
        }
        next.measurement_unit = (unit == MEASURE_UNIT_PMA) ? 1 : 0;
        changed++;
    }
    if ((value = cJSON_GetObjectItemCaseSensitive(root, "beep")) != NULL) {
        if (!as_whole_number(value, &number) ||
            !seconds_to_index(beep_timer_seconds, COUNT_OF(beep_timer_seconds), number, &next.beep_timer)) {
            *error = "beep: expected one of 0,60,180,300,600";
            return false;
        }
        changed++;
    }
    if ((value = cJSON_GetObjectItemCaseSensitive(root, "sleep")) != NULL) {
        if (!as_whole_number(value, &number) ||
            !seconds_to_index(sleep_timer_seconds, COUNT_OF(sleep_timer_seconds), number, &next.sleep_timer)) {
            *error = "sleep: expected one of 300,600,1800";
            return false;
        }
        changed++;
    }
    if ((value = cJSON_GetObjectItemCaseSensitive(root, "volume")) != NULL) {
        if (!as_whole_number(value, &number) || number < 0 || number > 100) {
            *error = "volume: expected 0-100";
            return false;
        }
        next.volume = percent_to_level(number, BUZZER_VOLUME_LEVEL_MAX);
        changed++;
    }
    if ((value = cJSON_GetObjectItemCaseSensitive(root, "brightness")) != NULL) {
        if (!as_whole_number(value, &number) || number < 0 || number > 100) {
            *error = "brightness: expected 0-100";
            return false;
        }
        next.brightness = percent_to_level(number, DISPLAY_BRIGHTNESS_LEVEL_MAX);
        changed++;
    }
    if ((value = cJSON_GetObjectItemCaseSensitive(root, "language")) != NULL) {
        bool known = false;
        for (size_t i = 0; cJSON_IsString(value) && i < COUNT_OF(languages); i++) {
            if (strcasecmp(value->valuestring, languages[i]) == 0) {
                snprintf(next.lang, sizeof(next.lang), "%s", languages[i]);
                known = true;
            }
        }
        if (!known) {
            *error = "language: expected en, hu, de, es or fr";
            return false;
        }
        changed++;
    }

    if (changed == 0) {
        *error = "no known config key";
        return false;
    }

    esp_err_t err = nvs_settings_save(&next);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not persist config: %s", esp_err_to_name(err));
        *error = "could not store settings";
        return false;
    }
    apply_runtime(&next);
    ESP_LOGI(TAG, "Config applied: %u key(s)", changed);
    return true;
}

int watch_settings_config_json(char *buf, size_t size)
{
    settings_t s;
    nvs_settings_load(&s);
    return snprintf(buf, size,
                    "{\"version\":2,\"type\":\"config\",\"unit\":\"%s\",\"beep\":%u,\"sleep\":%u,"
                    "\"volume\":%d,\"brightness\":%d,\"language\":\"%s\"}",
                    s.measurement_unit ? "pma" : "sec",
                    (unsigned)index_to_seconds(beep_timer_seconds, COUNT_OF(beep_timer_seconds), s.beep_timer),
                    (unsigned)index_to_seconds(sleep_timer_seconds, COUNT_OF(sleep_timer_seconds), s.sleep_timer),
                    level_to_percent(s.volume, BUZZER_VOLUME_LEVEL_MAX),
                    level_to_percent(s.brightness, DISPLAY_BRIGHTNESS_LEVEL_MAX),
                    s.lang);
}
