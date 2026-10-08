/**
 * @file measure.h
 * @brief Measurement manager module
 */

#ifndef MEASURE_H
#define MEASURE_H

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdbool.h>

#define GPIO_MEASURE_PIN GPIO_NUM_2
#define GPIO_PLUGGED_IN_PIN GPIO_NUM_3

/**
 * @brief Initialize measurement GPIOs and background task
 * @return ESP_OK on success
 */
esp_err_t measure_init(void);

/**
 * @brief Rewrite the status line from the current measurement state
 *
 * The status label is normally only touched when the state changes, so the
 * localization layer calls this after a language switch to re-emit the message
 * in the new language. Does nothing before measure_init().
 */
void measure_refresh_alert(void);

/**
 * @brief Re-evaluate the Save button after a BLE link change
 *
 * Saving sends the measurement to the phone, so the button is only active while
 * a phone is connected and subscribed to notifications. Call this whenever that
 * changes. Safe to call from the BLE host task: it only queues the work.
 */
void measure_refresh_save_state(void);

/**
 * @brief Which quantity the big number on the dashboard shows
 */
typedef enum {
    MEASURE_UNIT_TIME = 0,  /**< big number is the time, header is the PMA */
    MEASURE_UNIT_PMA,       /**< big number is the PMA, header is the time */
} measure_unit_t;

/**
 * @brief Swap the measured time and the PMA value, units included
 *
 * The big label and the small one in the header row trade places, and so do
 * their unit labels, so a value is never shown next to the wrong unit.
 *
 * @param unit Which quantity belongs in the big label
 */
void measure_set_unit(measure_unit_t unit);

/** @brief Currently selected unit */
measure_unit_t measure_get_unit(void);

/** @brief Boolean front end of measure_set_unit(), true selects the PMA value */
void measure_set_unit_pma(bool enabled);

/** @brief Boolean front end of measure_get_unit() */
bool measure_is_unit_pma(void);

/**
 * @brief Resolve a unit name coming from the phone
 * @param name  "sec" / "s" / "time", or "pma" / "l/m2/min" / "l/m²/min"
 * @param out   Filled on success
 * @return false when the name is not recognised
 */
bool measure_unit_from_name(const char *name, measure_unit_t *out);

/** @brief Short name of a unit, as reported back to the phone */
const char *measure_unit_name(measure_unit_t unit);

/**
 * @brief Rewrite the unit labels for the current language and unit selection
 * @note  Called by the localization layer after a language switch.
 */
void measure_refresh_units(void);

/**
 * @brief Request storing the last measurement, same as the Save button
 *
 * Safe to call from any task. The work itself happens on the measurement task.
 *
 * @return false when there is nothing to save, so the request was dropped
 */
bool measure_request_save(void);

/**
 * @brief Request discarding the last measurement, same as the Delete button
 *
 * Safe to call from any task. The work itself happens on the measurement task.
 *
 * @return false when there is nothing to delete, so the request was dropped
 */
bool measure_request_delete(void);

#endif /* MEASURE_H */
