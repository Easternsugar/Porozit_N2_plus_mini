/**
 * @file battery_ui.h
 * @brief Battery driven UI: status icon, charge-only mode, empty battery cutoff
 */

#ifndef BATTERY_UI_H
#define BATTERY_UI_H

#include <stdbool.h>

/** How long the "battery empty" warning stays on screen before powering off */
#define BATTERY_EMPTY_SHUTDOWN_MS   10000

/**
 * @brief Start the battery UI task
 * @note  Call after ui_init() and battery_monitor_init(). The task owns the
 *        periodic battery_monitor_update() calls, so it also drives the charge
 *        detection, and it powers the watch off when the battery runs empty.
 */
void battery_ui_start(void);

/**
 * @brief Show the charge screen and wait for the user to press the power button
 *
 * This is the "switched off, but on the cable" state. It is reached two ways:
 * USB power booting a watch that was off, and the user switching the watch off
 * while the cable is connected - the chip cannot actually power down then,
 * because VBUS keeps the 3V3 regulator alive through D4.
 *
 * Blocks until the power button is pressed, then latches the battery rail,
 * restores the normal idle timeout, wakes the display and returns. It does not
 * wait for the button to be released, so powering up feels the same as a normal
 * power-on. Unplugging the cable in this state cuts the rail and the watch
 * switches off on its own.
 *
 * @param drive_monitor Pass true when nothing else is calling
 *        battery_monitor_update() yet, i.e. during boot before
 *        battery_ui_start(). Pass false afterwards, otherwise this loop and the
 *        battery UI task would race on the charge detector state.
 *
 * @note Call after ui_init(); requires battery_monitor_init().
 */
void battery_ui_run_charge_screen(bool drive_monitor);

/**
 * @brief Check for an empty battery during boot and shut down if needed
 *
 * Returns immediately when the battery is fine or a charger is connected. When
 * the voltage is below BATTERY_CRITICAL_MV it shows the warning screen, waits
 * BATTERY_EMPTY_SHUTDOWN_MS and powers the watch off.
 *
 * @return true if the watch is powering off, so the caller must stop booting
 */
bool battery_ui_handle_empty_battery(void);

#endif /* BATTERY_UI_H */
