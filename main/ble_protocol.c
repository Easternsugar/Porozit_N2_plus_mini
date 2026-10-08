/**
 * @file ble_protocol.c
 * @brief JSON message protocol implementation (see ble_protocol.h)
 *
 * Messages are parsed and applied on a dedicated task, never in the BLE
 * callback: applying a setting writes NVS and relabels widgets, and blocking
 * the NimBLE host task for that long upsets the connection.
 */

#include "ble_protocol.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "battery_monitor.h"
#include "ble_service.h"
#include "buzzer.h"
#include "display_driver.h"
#include "i18n.h"
#include "measure.h"
#include "nvs_storage.h"
#include "power_save.h"

static const char *TAG = "ble_protocol";

#define BLE_PROTOCOL_TASK_STACK 4096
#define BLE_PROTOCOL_TASK_PRIO  4

typedef struct {
    uint16_t len;
    char json[BLE_PROTOCOL_MAX_MSG_LEN];
} ble_protocol_msg_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;

/**
 * @brief Answer an incoming message
 *
 * The phone treats a settings change as finished only when it finds "status"
 * set to "OK", and as failed on "ERROR" / "FAIL" / "NOK". Anything else leaves
 * the request hanging, so every reply has to carry one of those spellings
 * verbatim. "message" is the human readable detail next to it.
 *
 * @param request Short name of what was requested. Always a string literal
 *                from this file, never a value taken from the received JSON,
 *                so it cannot inject quotes into the reply.
 * @param ok      Whether the request was carried out
 * @param error   Reason when @p ok is false, may be NULL
 */
static void ble_protocol_send_result(const char *request, bool ok, const char *error)
{
    const char *message = ok ? BLE_PROTOCOL_STATUS_OK
                             : ((error != NULL) ? error : "failed");

    if (!ok) {
        ESP_LOGW(TAG, "%s rejected: %s", request, message);
    }

    char json[192];
    const int len = snprintf(json, sizeof(json),
                             "{\"version\":%d,\"type\":\"result\",\"request\":\"%s\","
                             "\"status\":\"%s\",\"message\":\"%s\"}",
                             BLE_PROTOCOL_VERSION, request,
                             ok ? BLE_PROTOCOL_STATUS_OK : BLE_PROTOCOL_STATUS_ERROR,
                             message);

    if (len <= 0 || (size_t)len >= sizeof(json)) {
        ESP_LOGE(TAG, "Reply for '%s' did not fit the buffer", request);
        return;
    }

    const esp_err_t err = ble_service_notify((const uint8_t *)json, (size_t)len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not send the reply for '%s': %s",
                 request, esp_err_to_name(err));
    }
}

esp_err_t ble_protocol_send_status(void)
{
    battery_status_t battery;
    battery_monitor_get_status(&battery);

    /* The stored indices are the phone's own representation of these settings,
     * so they are reported back under the same key names it writes. */
    settings_t settings;
    nvs_settings_load(&settings);

    char json[224];
    const int len = snprintf(json, sizeof(json),
                             "{\"version\":%d,\"type\":\"status\",\"status\":\"%s\","
                             "\"message\":\"%s\",\"battery\":%d,"
                             "\"charging\":%s,\"beepTimer\":%u,\"sleepTimer\":%u,"
                             "\"brightness\":%u,\"volume\":%u,"
                             "\"measurementUnit\":\"%s\","
                             "\"langSelector\":\"%s\"}",
                             BLE_PROTOCOL_VERSION,
                             BLE_PROTOCOL_STATUS_OK,
                             BLE_PROTOCOL_STATUS_OK,
                             battery.percentage,
                             battery.charging ? "true" : "false",
                             (unsigned)settings.beep_timer,
                             (unsigned)settings.sleep_timer,
                             (unsigned)settings.brightness,
                             (unsigned)settings.volume,
                             measure_unit_name(measure_get_unit()),
                             i18n_lang_code(i18n_get_lang()));

    if (len <= 0 || (size_t)len >= sizeof(json)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ble_service_notify((const uint8_t *)json, (size_t)len);
}

/**
 * @brief Read a JSON value as an integer
 * @return false when the value cannot be read as a number
 */
static bool ble_protocol_as_int(const cJSON *item, int *out)
{
    if (cJSON_IsNumber(item)) {
        *out = (int)item->valuedouble;
        return true;
    }

    /* Tolerate a number sent as a string */
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        char *end = NULL;
        const long value = strtol(item->valuestring, &end, 10);
        if (end != item->valuestring && (end == NULL || *end == '\0')) {
            *out = (int)value;
            return true;
        }
    }

    return false;
}

/* Beep intervals in seconds, index 0 is "off". Mirrors beepValues[] in the
 * previous (Arduino) firmware, whose settings row this key drives. */
static const uint16_t s_beep_timer_seconds[] = { 0, 60, 180, 300, 600 };

/* Auto sleep delays in minutes. Mirrors autoSleepValues[] in the previous
 * firmware, which stored 300 / 600 / 1800 seconds. */
static const uint16_t s_sleep_timer_minutes[] = { 5, 10, 30 };

#define BLE_PROTOCOL_COUNT(array) (sizeof(array) / sizeof((array)[0]))

/**
 * @brief "langSelector": switch the UI language
 *
 * Accepts the two letter code ("hu") and the English name ("Hungarian"), since
 * the previous firmware's table carried both.
 */
static const char *cfg_lang_selector(const cJSON *item)
{
    static const char *const names[] = {
        [LANG_EN] = "English",
        [LANG_HU] = "Hungarian",
        [LANG_DE] = "German",
        [LANG_ES] = "Spanish",
        [LANG_FR] = "French",
    };

    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return "langSelector: expected a string";
    }

    const char *value = item->valuestring;
    lang_id_t lang = LANG_COUNT;

    /* i18n_lang_from_code() falls back to English for anything it does not
     * know, so the round trip is checked instead of trusting the result. */
    const lang_id_t by_code = i18n_lang_from_code(value);
    if (strcasecmp(value, i18n_lang_code(by_code)) == 0) {
        lang = by_code;
    } else {
        for (int i = 0; i < LANG_COUNT; i++) {
            if (strcasecmp(value, names[i]) == 0) {
                lang = (lang_id_t)i;
                break;
            }
        }
    }

    if (lang == LANG_COUNT) {
        ESP_LOGW(TAG, "langSelector: cannot map '%s'", value);
        return "langSelector: unknown language";
    }

    /* Persists the choice and relabels every widget */
    i18n_set_lang(lang);

    ESP_LOGI(TAG, "langSelector '%s' -> %s", value, i18n_lang_code(lang));
    return NULL;
}

/**
 * @brief "beepTimer": index into the beep interval table, 0 turns the beep off
 *
 * The watch has no periodic reminder beep yet, so only the on/off part of this
 * setting takes effect. The index itself is stored, so the interval can start
 * working without the phone having to resend anything.
 */
static const char *cfg_beep_timer(const cJSON *item)
{
    int index;
    if (!ble_protocol_as_int(item, &index)) {
        return "beepTimer: expected a number";
    }

    if (index < 0 || (size_t)index >= BLE_PROTOCOL_COUNT(s_beep_timer_seconds)) {
        ESP_LOGW(TAG, "beepTimer: %d is out of range", index);
        return "beepTimer: index out of range, expected 0-4";
    }

    const bool enabled = (s_beep_timer_seconds[index] != 0);

    buzzer_set_enabled(enabled);
    nvs_settings_set_beep(enabled);
    nvs_settings_set_beep_timer((uint8_t)index);

    ESP_LOGI(TAG, "beepTimer index %d (%u s) -> beep %s",
             index, (unsigned)s_beep_timer_seconds[index], enabled ? "on" : "off");
    return NULL;
}

/**
 * @brief "sleepTimer": index into the auto sleep table
 */
static const char *cfg_sleep_timer(const cJSON *item)
{
    int index;
    if (!ble_protocol_as_int(item, &index)) {
        return "sleepTimer: expected a number";
    }

    if (index < 0 || (size_t)index >= BLE_PROTOCOL_COUNT(s_sleep_timer_minutes)) {
        ESP_LOGW(TAG, "sleepTimer: %d is out of range", index);
        return "sleepTimer: index out of range, expected 0-2";
    }

    const uint32_t minutes = s_sleep_timer_minutes[index];

    power_save_set_timeout_ms(minutes * 60U * 1000U);
    nvs_settings_set_sleep_timer((uint8_t)index);

    ESP_LOGI(TAG, "sleepTimer index %d -> %u minutes", index, (unsigned)minutes);
    return NULL;
}

/**
 * @brief "brightness": backlight level, 0 to DISPLAY_BRIGHTNESS_LEVEL_MAX
 */
static const char *cfg_brightness(const cJSON *item)
{
    int level;
    if (!ble_protocol_as_int(item, &level)) {
        return "brightness: expected a number";
    }

    if (level < 0 || level > DISPLAY_BRIGHTNESS_LEVEL_MAX) {
        ESP_LOGW(TAG, "brightness: %d is out of range", level);
        return "brightness: out of range, expected 0-6";
    }

    display_set_brightness_level((uint8_t)level);
    nvs_settings_set_brightness((uint8_t)level);
    return NULL;
}

/**
 * @brief "volume": buzzer level, 0 to BUZZER_VOLUME_LEVEL_MAX, 0 is silent
 */
static const char *cfg_volume(const cJSON *item)
{
    int level;
    if (!ble_protocol_as_int(item, &level)) {
        return "volume: expected a number";
    }

    if (level < 0 || level > BUZZER_VOLUME_LEVEL_MAX) {
        ESP_LOGW(TAG, "volume: %d is out of range", level);
        return "volume: out of range, expected 0-6";
    }

    buzzer_set_volume_level((uint8_t)level);
    nvs_settings_set_volume((uint8_t)level);
    return NULL;
}

/**
 * @brief "measurementUnit": which quantity the big number shows
 *
 * Swaps the measured time and the PMA value, each with its own unit label.
 */
static const char *cfg_measurement_unit(const cJSON *item)
{
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return "measurementUnit: expected a string";
    }

    measure_unit_t unit;
    if (!measure_unit_from_name(item->valuestring, &unit)) {
        ESP_LOGW(TAG, "measurementUnit: cannot map '%s'", item->valuestring);
        return "measurementUnit: expected \"sec\" or \"pma\"";
    }

    measure_set_unit(unit);
    nvs_settings_set_measurement_unit((uint8_t)unit);
    return NULL;
}

/**
 * @brief Log every top-level key and value of a received message
 *
 * Unknown keys are dumped as well: this is how the exact names, types and
 * value ranges the phone uses become visible without having to guess them.
 */
static void ble_protocol_log_members(const cJSON *root)
{
    const cJSON *item = NULL;

    cJSON_ArrayForEach(item, root) {
        const char *key = (item->string != NULL) ? item->string : "(unnamed)";

        if (cJSON_IsString(item)) {
            ESP_LOGI(TAG, "  %s = \"%s\"  (string)", key,
                     (item->valuestring != NULL) ? item->valuestring : "");
        } else if (cJSON_IsBool(item)) {
            ESP_LOGI(TAG, "  %s = %s  (bool)", key, cJSON_IsTrue(item) ? "true" : "false");
        } else if (cJSON_IsNumber(item)) {
            /* Both forms, an Int from the app still arrives as a double here */
            ESP_LOGI(TAG, "  %s = %d / %.3f  (number)", key,
                     item->valueint, item->valuedouble);
        } else if (cJSON_IsNull(item)) {
            ESP_LOGI(TAG, "  %s = null", key);
        } else {
            char *text = cJSON_PrintUnformatted(item);
            ESP_LOGI(TAG, "  %s = %s  (%s)", key, (text != NULL) ? text : "?",
                     cJSON_IsArray(item) ? "array" : "object");
            cJSON_free(text);
        }
    }
}

typedef struct {
    const char *key;
    /** NULL for a key this firmware knows about but cannot act on yet */
    const char *(*apply)(const cJSON *item);
} ble_protocol_config_key_t;

/** Keys the phone sends in a "config" message */
static const ble_protocol_config_key_t s_config_keys[] = {
    { "langSelector",    cfg_lang_selector },
    { "beepTimer",       cfg_beep_timer },
    { "sleepTimer",      cfg_sleep_timer },
    { "volume",          cfg_volume },
    { "brightness",      cfg_brightness },
    { "measurementUnit", cfg_measurement_unit },
};

/**
 * @brief Apply every known key in a config message
 *
 * A key with no feature behind it is skipped, not treated as a failure: the app
 * keeps all its settings in one object, so a message can easily carry
 * langSelector and brightness together. Failing the whole message on the
 * unsupported one made the phone discard a language change that the watch had
 * in fact already applied. Only a bad value for a supported key fails.
 */
static void ble_protocol_apply_config(const cJSON *root)
{
    const char *error = NULL;
    unsigned applied = 0;
    unsigned skipped = 0;

    for (size_t i = 0; i < BLE_PROTOCOL_COUNT(s_config_keys); i++) {
        const ble_protocol_config_key_t *entry = &s_config_keys[i];

        const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, entry->key);
        if (item == NULL) {
            continue;
        }

        if (entry->apply == NULL) {
            ESP_LOGW(TAG, "%s is not supported by this firmware, ignored", entry->key);
            skipped++;
            continue;
        }

        const char *key_error = entry->apply(item);
        if (key_error != NULL) {
            error = key_error;
            break;
        }

        applied++;
    }

    if (error == NULL && applied == 0) {
        error = (skipped > 0) ? "only unsupported keys in this message"
                              : "no known config key";
    }

    if (applied > 0) {
        /* Push the new values into the settings menu. i18n_set_lang() already
         * does this, but a message without a language would not have. */
        i18n_apply_ui();

        ESP_LOGI(TAG, "Config applied: %u key(s), %u ignored", applied, skipped);
    }

    ble_protocol_send_result("config", error == NULL, error);
}

static void ble_protocol_handle_action(const char *action)
{
    if (action == NULL) {
        ble_protocol_send_result("command", false, "missing action");
        return;
    }

    if (strcasecmp(action, "save") == 0) {
        const bool ok = measure_request_save();
        ble_protocol_send_result("save", ok, ok ? NULL : "nothing to save");
    } else if (strcasecmp(action, "delete") == 0) {
        const bool ok = measure_request_delete();
        ble_protocol_send_result("delete", ok, ok ? NULL : "nothing to delete");
    } else if (strcasecmp(action, "status") == 0) {
        const esp_err_t err = ble_protocol_send_status();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Could not send the status: %s", esp_err_to_name(err));
        }
    } else {
        ble_protocol_send_result("command", false, "unknown action");
    }
}

static void ble_protocol_dispatch(const char *json, size_t len)
{
    /* Raw payload first: when parsing fails this is the only trace left, and it
     * shows the exact bytes that arrived. */
    ESP_LOGI(TAG, "RX %u bytes: %s", (unsigned)len, json);

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == NULL) {
        ble_protocol_send_result("unknown", false, "malformed json");
        return;
    }

    /* Dump the contents before any validation, so even a message this firmware
     * rejects still shows what the phone sent. */
    ble_protocol_log_members(root);

    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || (int)version->valuedouble != BLE_PROTOCOL_VERSION) {
        ble_protocol_send_result("unknown", false, "unsupported version");
        cJSON_Delete(root);
        return;
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || type->valuestring == NULL) {
        ble_protocol_send_result("unknown", false, "missing type");
        cJSON_Delete(root);
        return;
    }

    const char *type_str = type->valuestring;

    if (strcasecmp(type_str, "config") == 0) {
        ble_protocol_apply_config(root);
    } else if (strcasecmp(type_str, "command") == 0) {
        /* Accept either field name for the action */
        const cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
        if (action == NULL) {
            action = cJSON_GetObjectItemCaseSensitive(root, "command");
        }
        ble_protocol_handle_action(cJSON_IsString(action) ? action->valuestring : NULL);
    } else if (strcasecmp(type_str, "save") == 0 ||
               strcasecmp(type_str, "delete") == 0 ||
               strcasecmp(type_str, "status") == 0) {
        /* Action carried directly in the type field */
        ble_protocol_handle_action(type_str);
    } else {
        ble_protocol_send_result("unknown", false, "unknown type");
    }

    cJSON_Delete(root);
}

static void ble_protocol_task(void *param)
{
    (void)param;

    ESP_LOGI(TAG, "Protocol task started");

    while (1) {
        ble_protocol_msg_t msg;
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) == pdTRUE) {
            ble_protocol_dispatch(msg.json, msg.len);
        }
    }
}

esp_err_t ble_protocol_init(void)
{
    if (s_task != NULL) {
        return ESP_OK;
    }

    s_queue = xQueueCreate(BLE_PROTOCOL_QUEUE_LEN, sizeof(ble_protocol_msg_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create the message queue");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(ble_protocol_task, "ble_proto", BLE_PROTOCOL_TASK_STACK, NULL,
                    BLE_PROTOCOL_TASK_PRIO, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the protocol task");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

void ble_protocol_receive(const uint8_t *data, size_t len)
{
    if (s_queue == NULL || data == NULL || len == 0) {
        return;
    }

    if (len >= BLE_PROTOCOL_MAX_MSG_LEN) {
        ESP_LOGW(TAG, "Dropping a %u byte message, the limit is %d",
                 (unsigned)len, BLE_PROTOCOL_MAX_MSG_LEN - 1);
        return;
    }

    ble_protocol_msg_t msg;
    memcpy(msg.json, data, len);
    msg.json[len] = '\0';
    msg.len = (uint16_t)len;

    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Queue full, message dropped");
    }
}
