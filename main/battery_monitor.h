/**
 * @file battery_monitor.h
 * @brief Battery voltage monitoring and charge (USB plugged in) detection
 */

#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdbool.h>
#include "esp_err.h"

/* ADC settings */
#define BATTERY_ADC_UNIT        ADC_UNIT_1
#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_0
#define BATTERY_ADC_ATTEN       ADC_ATTEN_DB_12

/* Voltage divider ratio (R3 200k / R7 100k on BAT_ADC -> GPIO1) */
#define BATTERY_VOLTAGE_DIVIDER_RATIO   3.0f

/* Number of samples to average in battery_monitor_read_voltage() */
#define BATTERY_ADC_SAMPLES     10

/* Samples averaged per battery_monitor_update() call (taken back to back) */
#define BATTERY_ADC_FAST_SAMPLES 16

/* Below this the watch shuts itself down. The ME6217C33 LDO needs roughly
 * 3.5 V to hold 3.3 V under load, so there is no point in running lower. */
#define BATTERY_CRITICAL_MV     3300

/* Li-Ion mapping used for the percentage. 0% is deliberately the cutoff
 * voltage, not the 3.0 V a bare cell can reach, so that the gauge hits 0% at
 * the moment the watch actually shuts down. */
#define BATTERY_EMPTY_MV        BATTERY_CRITICAL_MV
#define BATTERY_FULL_MV         4200
/* Consecutive samples below BATTERY_CRITICAL_MV before acting on it, so that a
 * buzzer beep or a BLE transmit burst cannot trigger a shutdown */
#define BATTERY_CRITICAL_SAMPLES 6

/* -------------------------------------------------------------------------
 * Charge detection
 *
 * The ESP32-S3-Touch-LCD-1.69 V2.1 schematic does not route the ETA6098 STAT
 * pin (U9 pin 9 is left unconnected) nor VBUS to any ESP32 GPIO, and the
 * ESP32-S3 internal USB PHY has no VBUS-detect logic. The only power related
 * signal available to software is BAT_ADC (B+ through a 200k/100k divider on
 * GPIO1), so "charging" has to be inferred from the battery voltage.
 *
 * Plugging in the USB cable does two things at once: the system load moves off
 * the cell (VBUS feeds the 3V3 regulator through D4) and the charger starts
 * pushing current into it. Both push B+ up, so a plug/unplug event shows up as
 * a clear voltage step (typically 100-200 mV) which is what is detected here.
 * The slow trend catches the case where the watch is booted already on a
 * charger, where no step is ever seen.
 *
 * If you want an exact reading, wire VBUS (test point TP1) through a
 * 100k/100k divider to a free GPIO - GPIO9 and GPIO12 are unused on this
 * board - and set BATTERY_CHARGE_GPIO to it. The heuristic is then bypassed.
 * ------------------------------------------------------------------------- */

/** GPIO wired to a charger/VBUS status signal, or -1 to use the heuristic */
#define BATTERY_CHARGE_GPIO             (-1)
/** Level that means "charger present" on BATTERY_CHARGE_GPIO */
#define BATTERY_CHARGE_GPIO_ACTIVE_HIGH (1)

/** How often battery_monitor_update() is expected to be called [ms] */
#define BATTERY_SAMPLE_PERIOD_MS        500

/** Voltage step that marks a plug/unplug event [mV] */
#define BATTERY_CHG_STEP_MV             60
/** Length of the step detection window, in samples (6 * 500 ms = 3 s) */
#define BATTERY_CHG_STEP_SAMPLES        6
/** Consecutive samples that must agree before the state flips */
#define BATTERY_CHG_CONFIRM_SAMPLES     2

/** Rise over BATTERY_CHG_TREND_SLOTS * BATTERY_CHG_TREND_DECIM samples [mV] */
#define BATTERY_CHG_TREND_MV            40
/** One trend slot every N samples (20 * 500 ms = 10 s) */
#define BATTERY_CHG_TREND_DECIM         20
/** Number of trend slots (12 * 10 s = 120 s window) */
#define BATTERY_CHG_TREND_SLOTS         12

/** Voltage above which the state is seeded to "charging" at start-up [mV] */
#define BATTERY_CHG_SEED_MV             4150

/**
 * @brief Latest battery state as maintained by battery_monitor_update()
 */
typedef struct {
    int  voltage_mv;    /*!< Filtered battery voltage in millivolts */
    int  percentage;    /*!< Battery percentage (0-100) */
    bool charging;      /*!< true while a charger is detected */
    bool valid;         /*!< false until the first successful sample */
} battery_status_t;

/**
 * @brief Initialize battery monitoring ADC (and the charge status GPIO)
 * @return ESP_OK on success
 */
esp_err_t battery_monitor_init(void);

/**
 * @brief Read battery voltage (blocking, ~100 ms)
 * @param voltage_mv Pointer to store voltage in millivolts (after divider compensation)
 * @return ESP_OK on success
 */
esp_err_t battery_monitor_read_voltage(int *voltage_mv);

/**
 * @brief Get battery percentage (simple linear approximation)
 * @param percentage Pointer to store battery percentage (0-100)
 * @return ESP_OK on success
 */
esp_err_t battery_monitor_get_percentage(int *percentage);

/**
 * @brief Take one sample, run the charge detection and publish the result
 * @note  Must be called periodically every BATTERY_SAMPLE_PERIOD_MS for the
 *        charge detection to work. battery_ui owns that loop.
 * @param status Optional, filled with the new state
 * @return ESP_OK on success
 */
esp_err_t battery_monitor_update(battery_status_t *status);

/**
 * @brief Override the initial charge state with a known value
 * @note  Normally the first sample only guesses from the absolute voltage. Call
 *        this when the caller has hard evidence, e.g. the watch booted because
 *        USB power was applied, which proves a charger is connected.
 * @param charging Known charge state
 */
void battery_monitor_seed_charging(bool charging);

/**
 * @brief Get the state published by the last battery_monitor_update()
 * @param status Filled with the cached state (never NULL)
 */
void battery_monitor_get_status(battery_status_t *status);

/**
 * @brief Shorthand for battery_monitor_get_status().charging
 * @return true while a charger is detected
 */
bool battery_monitor_is_charging(void);

/**
 * @brief Deinitialize battery monitoring (free resources)
 */
void battery_monitor_deinit(void);

#endif /* BATTERY_MONITOR_H */
