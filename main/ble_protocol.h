/**
 * @file ble_protocol.h
 * @brief JSON message protocol spoken over the BLE characteristic
 *
 * Every message is a flat JSON object wrapped in the same envelope the watch
 * already uses for its measurement notifications:
 *
 *     { "version": 1, "type": <string>, ... }
 *
 * INCOMING (phone -> watch)
 *
 *   Settings, one key per message as the app sends them:
 *     {"version":1,"type":"config","langSelector":"hu"}   EN/HU/DE/IT/FR code
 *                                                         or English name
 *     {"version":1,"type":"config","beepTimer":2}         0..4, 0 = beep off
 *     {"version":1,"type":"config","sleepTimer":1}        0..2 = 5/10/30 min
 *     {"version":1,"type":"config","volume":6}            0..6, 0 = silent
 *     {"version":1,"type":"config","brightness":6}        0..6, 0 = dimmest
 *     {"version":1,"type":"config","measurementUnit":"…"} not supported yet
 *
 *   Actions:
 *     {"version":1,"type":"command","action":"save"}
 *     {"version":1,"type":"command","action":"delete"}
 *     {"version":1,"type":"command","action":"status"}
 *
 *   The action may also be carried directly in the type field, so
 *   {"version":1,"type":"save"} works too.
 *
 * OUTGOING (watch -> phone)
 *
 *   Every reply carries "status", which is exactly "OK" or "ERROR" because that
 *   is what the phone matches on, plus a human readable "message":
 *     {"version":1,"type":"result","request":"config","status":"OK","message":"OK"}
 *     {"version":1,"type":"result","request":"save","status":"ERROR",
 *      "message":"nothing to save"}
 *
 *   A status request replies with the settings under the same key names:
 *     {"version":1,"type":"status","status":"OK","message":"OK","battery":87,
 *      "charging":false,"beepTimer":2,"sleepTimer":0,"langSelector":"HU"}
 */

#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** Protocol version this firmware understands */
#define BLE_PROTOCOL_VERSION     1

/* The "status" field the phone reads to decide whether a settings change
 * completed. It only accepts these exact spellings. */
#define BLE_PROTOCOL_STATUS_OK    "OK"
#define BLE_PROTOCOL_STATUS_ERROR "ERROR"

/* Longest incoming message, including the terminating NUL. Kept above the
 * transport limit in ble_service.c so nothing the characteristic accepts gets
 * dropped here instead. */
#define BLE_PROTOCOL_MAX_MSG_LEN 256

/** Incoming messages buffered while the protocol task is busy */
#define BLE_PROTOCOL_QUEUE_LEN   4

/**
 * @brief Create the queue and task that process incoming messages
 * @note  Call after ble_service_init(), measure_init() and the settings restore.
 * @return ESP_OK on success
 */
esp_err_t ble_protocol_init(void);

/**
 * @brief Hand a received payload over to the protocol task
 *
 * Copies the payload and returns immediately. Meant to be called from the
 * NimBLE host task, which must never block on NVS writes or the LVGL lock.
 *
 * @param data Raw characteristic value, not NUL terminated
 * @param len  Number of bytes in @p data
 */
void ble_protocol_receive(const uint8_t *data, size_t len);

/**
 * @brief Send the current watch state to the phone
 * @return Result of the notification, ESP_ERR_INVALID_STATE when not subscribed
 */
esp_err_t ble_protocol_send_status(void);

#endif /* BLE_PROTOCOL_H */
