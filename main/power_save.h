/**
 * @file power_save.h
 * @brief Idle sleep: darkens the screen and quiets the power hungry parts
 *
 * After a period without user interaction the backlight and the LCD are turned
 * off, LVGL stops rendering and polling, and BLE stops advertising. Any user
 * interaction brings everything back at full brightness.
 *
 * The short timeout applies only in charge-only mode, that is when the watch
 * was off and USB power booted it just to show the charge screen. A watch the
 * user switched on keeps the normal timeout even when the cable is plugged in,
 * because somebody is using it.
 */

#ifndef POWER_SAVE_H
#define POWER_SAVE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/** Idle time before a switched-on watch goes to sleep, cable or not */
#define POWER_SAVE_TIMEOUT_MS             (30 * 60 * 1000)
/** Idle time before the charge-only screen goes dark */
#define POWER_SAVE_TIMEOUT_CHARGE_ONLY_MS (10 * 1000)

/**
 * @brief Start the idle watchdog task
 * @note  Call after display_lvgl_init(), touch_driver_init() and ui_init().
 * @return ESP_OK on success
 */
esp_err_t power_save_start(void);

/**
 * @brief Report user interaction: resets the idle timer and wakes the watch
 *
 * Lock free and safe to call from any task, including while holding the LVGL
 * lock. The actual wake up is performed by the power save task, so this returns
 * before the screen is back on. Use power_save_wake() when the caller needs the
 * display to be up before it continues.
 */
void power_save_notify_activity(void);

/**
 * @brief Wake the watch up and wait for the display to be back on
 * @note  Do not call from an ISR or while holding the LVGL lock.
 */
void power_save_wake(void);

/**
 * @brief Select the charge-only timeout instead of the normal one
 *
 * Charge-only mode is entered when USB power boots a watch that was switched
 * off. Leaving it, which happens when the user presses the power button and the
 * normal boot continues, must switch back to the long timeout even though the
 * cable is still connected. Also resets the idle timer.
 *
 * @param charge_only true while showing the charge-only screen
 */
void power_save_set_charge_only(bool charge_only);

/**
 * @brief Change the normal idle timeout
 *
 * Does not affect the charge-only timeout. Also resets the idle timer, so the
 * new value gets a full window instead of firing straight away.
 *
 * @param timeout_ms Idle time before the watch sleeps, 0 restores the default
 */
void power_save_set_timeout_ms(uint32_t timeout_ms);

/**
 * @brief Whether the watch is currently asleep
 */
bool power_save_is_sleeping(void);

/** Set the normal inactivity timeout in seconds (300, 600 or 1800). */
void power_save_set_timeout(uint16_t seconds);

#endif /* POWER_SAVE_H */
