/**
 * @file power_manager.h
 * @brief Power management module - handles power button and power enable
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "esp_err.h"
#include "driver/gpio.h"

/* Power GPIOs */
#define PWR_BTN_GPIO    GPIO_NUM_40
#define PWR_EN_GPIO     GPIO_NUM_41

/* Power off hold time in milliseconds */
#define POWER_OFF_HOLD_TIME_MS  2000

/* Power button polling interval */
#define POWER_BTN_POLL_MS       50

/* Grace period after dropping PWR_EN. On battery the rail is gone long before
 * this elapses, so still running afterwards means USB power is present. */
#define POWER_OFF_SETTLE_MS     200

/**
 * @brief Initialize power management (GPIO setup and power enable)
 * @return ESP_OK on success
 */
esp_err_t power_manager_init(void);

/**
 * @brief Start power button monitoring task
 * @note This will create a FreeRTOS task that monitors the power button
 *       and triggers shutdown if held for POWER_OFF_HOLD_TIME_MS
 * @return ESP_OK on success
 */
esp_err_t power_manager_start_monitoring(void);

/**
 * @brief Latch the battery power rail on
 * @note  power_manager_init() only latches it when the watch was started with
 *        the power button. Call this when the user powers the watch up from
 *        charge-only mode, otherwise it would die as soon as USB is unplugged.
 */
void power_manager_power_on(void);

/**
 * @brief Trigger power off immediately
 * @note  This drops PWR_EN, which disconnects the battery. While USB is
 *        plugged in the 3V3 rail is fed from VBUS through D4, so the watch
 *        keeps running until the cable is removed.
 */
void power_manager_power_off(void);

/**
 * @brief Whether the power button was held down when the watch booted
 *
 * The battery only reaches the system through Q5, which the power button
 * latches. VBUS on the other hand feeds the 3V3 regulator directly through D4,
 * so the watch also boots when USB is plugged into a powered-off watch. In that
 * case the button is not pressed, which is how the two are told apart.
 *
 * @return true if the user started the watch, false if the charger did
 */
bool power_manager_booted_from_button(void);

/**
 * @brief Check if power button is currently pressed
 * @return true if pressed, false otherwise
 */
bool power_manager_is_button_pressed(void);

#endif /* POWER_MANAGER_H */
