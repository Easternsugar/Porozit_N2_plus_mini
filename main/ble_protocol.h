/**
 * @file ble_protocol.h
 * @brief Porozit BLE protocol v2 over the 0xFFF1 characteristic (see PROTOCOL.md)
 *
 * Incoming JSON is queued from the NimBLE host task and handled on a dedicated
 * task, because applying it writes NVS and takes the LVGL lock. Outgoing
 * messages are built here, except the measurement events, which measure.c
 * sends as they happen.
 */

#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** Protocol version this firmware speaks */
#define BLE_PROTOCOL_VERSION     2

/* Longest incoming message, including the terminating NUL */
#define BLE_PROTOCOL_MAX_MSG_LEN 256

/** Incoming messages buffered while the protocol task is busy */
#define BLE_PROTOCOL_QUEUE_LEN   4

/**
 * @brief Create the queue and task that process incoming messages
 * @note  Call after ble_service_init(), measure_init() and watch_settings_init().
 */
esp_err_t ble_protocol_init(void);

/**
 * @brief Hand a received payload over to the protocol task
 *
 * Copies the payload and returns immediately. Meant to be called from the
 * NimBLE host task, which must never block on NVS writes or the LVGL lock.
 */
void ble_protocol_receive(const uint8_t *data, size_t len);

/** Send the "state" message (where the measurement cycle is) */
esp_err_t ble_protocol_send_state(void);

/** Send the "battery" message; called by the heartbeat task every 10 s */
esp_err_t ble_protocol_send_battery(void);

#endif /* BLE_PROTOCOL_H */
