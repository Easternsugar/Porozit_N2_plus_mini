/**
 * @file ble_protocol.c
 * @brief Porozit BLE protocol v2 (see PROTOCOL.md)
 *
 * Messages are parsed and applied on a dedicated task, never in the BLE
 * callback: applying a setting writes NVS and relabels widgets, and blocking
 * the NimBLE host task for that long upsets the connection.
 */

#include "ble_protocol.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "battery_monitor.h"
#include "ble_service.h"
#include "measure.h"
#include "watch_settings.h"

static const char *TAG = "ble_protocol";

#define BLE_PROTOCOL_TASK_STACK 4096
#define BLE_PROTOCOL_TASK_PRIO  4

typedef struct {
    uint16_t len;
    char json[BLE_PROTOCOL_MAX_MSG_LEN];
} ble_protocol_msg_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;

static esp_err_t send_json(const char *json, int len)
{
    if (len <= 0 || (size_t)len >= 240) {
        ESP_LOGE(TAG, "Message too long (%d bytes), not sent", len);
        return ESP_ERR_INVALID_SIZE;
    }
    return ble_service_notify((const uint8_t *)json, (size_t)len);
}

/**
 * @brief Answer a command or config write
 * @param request Always a string literal from this file, never a value taken
 *                from the received JSON, so it cannot inject quotes.
 * @param error   NULL on success, else a static reason without quotes
 */
static void send_result(const char *request, const char *error)
{
    if (error != NULL) {
        ESP_LOGW(TAG, "%s rejected: %s", request, error);
    }
    char json[200];
    const int len = snprintf(json, sizeof(json),
                             "{\"version\":%d,\"type\":\"result\",\"request\":\"%s\","
                             "\"status\":\"%s\",\"message\":\"%s\"}",
                             BLE_PROTOCOL_VERSION, request, error == NULL ? "OK" : "ERROR",
                             error == NULL ? "OK" : error);
    send_json(json, len);
}

static esp_err_t send_info(void)
{
    char json[160];
    const int len = snprintf(json, sizeof(json),
                             "{\"version\":%d,\"type\":\"info\",\"model\":\"mini\",\"fw\":\"%s\","
                             "\"serial\":\"\",\"maxSaved\":0}",
                             BLE_PROTOCOL_VERSION, esp_app_get_description()->version);
    return send_json(json, len);
}

static esp_err_t send_config(void)
{
    char json[200];
    return send_json(json, watch_settings_config_json(json, sizeof(json)));
}

esp_err_t ble_protocol_send_state(void)
{
    char json[160];
    return send_json(json, measure_state_json(json, sizeof(json)));
}

esp_err_t ble_protocol_send_battery(void)
{
    battery_status_t battery;
    battery_monitor_get_status(&battery);
    if (!battery.valid) {
        return ESP_ERR_INVALID_STATE;
    }
    char json[96];
    const int len = snprintf(json, sizeof(json),
                             "{\"version\":%d,\"type\":\"battery\",\"battery\":%d,\"charging\":%s}",
                             BLE_PROTOCOL_VERSION, battery.percentage,
                             battery.charging ? "true" : "false");
    return send_json(json, len);
}

static void handle_command(const char *action)
{
    if (action == NULL) {
        send_result("unknown", "missing action");
    } else if (strcasecmp(action, "save") == 0) {
        send_result("save", measure_request_save() ? NULL : "nothing to save");
    } else if (strcasecmp(action, "delete") == 0) {
        send_result("delete", measure_request_delete() ? NULL : "nothing to delete");
    } else if (strcasecmp(action, "reset") == 0) {
        send_result("reset", measure_request_reset() ? NULL : "measurement running");
    } else if (strcasecmp(action, "list") == 0) {
        /* The N2+ mini stores no results (maxSaved 0): nothing to list. */
        send_result("list", NULL);
        ble_protocol_send_state();
    } else if (strcasecmp(action, "status") == 0) {
        send_info();
        send_config();
        ble_protocol_send_state();
        ble_protocol_send_battery();
    } else {
        send_result("unknown", "unknown action");
    }
}

static void dispatch(const char *json, size_t len)
{
    ESP_LOGI(TAG, "RX %u bytes: %s", (unsigned)len, json);

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        send_result("unknown", "malformed json");
        return;
    }

    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsNumber(version) || (int)version->valuedouble != BLE_PROTOCOL_VERSION) {
        send_result("unknown", "unsupported version, expected 2");
    } else if (!cJSON_IsString(type)) {
        send_result("unknown", "missing type");
    } else if (strcmp(type->valuestring, "config") == 0) {
        const char *error = NULL;
        if (watch_settings_apply(root, &error)) {
            send_result("config", NULL);
            send_config();
        } else {
            send_result("config", error);
        }
    } else if (strcmp(type->valuestring, "command") == 0) {
        const cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
        handle_command(cJSON_IsString(action) ? action->valuestring : NULL);
    } else {
        send_result("unknown", "unknown type");
    }

    cJSON_Delete(root);
}

static void ble_protocol_task(void *param)
{
    (void)param;
    ble_protocol_msg_t msg;
    while (1) {
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) == pdTRUE) {
            dispatch(msg.json, msg.len);
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
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(ble_protocol_task, "ble_proto", BLE_PROTOCOL_TASK_STACK, NULL,
                    BLE_PROTOCOL_TASK_PRIO, &s_task) != pdPASS) {
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
