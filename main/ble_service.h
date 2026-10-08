#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#include <stdbool.h>

esp_err_t ble_service_init(void);
esp_err_t ble_service_notify(const uint8_t *data, size_t len);

/**
 * @brief Whether a notification can actually reach the phone right now
 *
 * True only when a phone is connected AND it has subscribed to notifications,
 * because NimBLE refuses to notify without the CCCD write. This is the
 * condition for anything that reports data to the phone, such as saving a
 * measurement.
 */
bool ble_service_can_notify(void);

/**
 * @brief Stop or restart advertising to save power while the watch is asleep
 *
 * An already established connection is left alone, only the advertising radio
 * traffic is stopped. Safe to call before ble_service_init(), in which case it
 * does nothing.
 *
 * @param low_power true to stop advertising, false to advertise again
 */
void ble_service_set_low_power(bool low_power);
