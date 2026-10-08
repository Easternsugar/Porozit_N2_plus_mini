/**
 * @file ui_screens.h
 * @brief Thread-safe helpers for switching between SquareLine screens.
 *
 * These helpers live OUTSIDE the SquareLine-generated `ui/` folder so they are
 * not overwritten on UI re-export. They wrap the screen switch in the LVGL
 * port mutex, which is mandatory because LVGL is not thread-safe.
 */

#ifndef UI_SCREENS_H
#define UI_SCREENS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Show the loading/splash screen (HELLO logo, no goodbye text).
 *
 * Used at boot. Safe to call from any task; it takes the LVGL lock internally.
 */
void ui_show_load_screen(void);

/**
 * @brief Show the main dashboard screen.
 *
 * Safe to call from any task; it takes the LVGL lock internally.
 */
void ui_show_dashboard_screen(void);

/**
 * @brief Show the charge screen in "charging" state (charge icon + text).
 *
 * Used when USB is plugged into a powered-off watch: the watch boots from VBUS
 * but must not start the application, only indicate that it is charging.
 * Safe to call from any task; takes the LVGL lock internally.
 */
void ui_show_charge_screen(void);

/**
 * @brief Show the charge screen in "battery empty" state.
 *
 * Reuses the same screen with the empty battery icon and a warning text. The
 * caller is expected to power the watch off shortly after.
 * Safe to call from any task; takes the LVGL lock internally.
 */
void ui_show_battery_empty_screen(void);

/**
 * @brief Show the load screen with the goodbye message (ui_bye) revealed.
 *
 * Used on shutdown. Safe to call from any task; takes the LVGL lock internally.
 *
 * @param message Text to display, or NULL to keep the screen's default text.
 */
void ui_show_bye_screen(const char *message);

/**
 * @brief Take over the slide-up menu swipe handling from SquareLine.
 *
 * The generated handler animates the panel by a fixed relative offset on every
 * swipe without checking whether the menu is already open or closed, so
 * repeated swipes push it past both end positions. This installs a state driven
 * handler that animates to absolute positions instead.
 *
 * Call once after ui_init().
 */
void ui_menu_init(void);

/**
 * @brief Show or hide the Bluetooth connected icon (ui_bluetooth).
 *
 * Call with true when a phone connects, false when it disconnects.
 * Safe to call from any task; takes the LVGL lock internally.
 *
 * @param connected true to show the icon, false to hide it.
 */
void ui_set_bluetooth_connected(bool connected);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* UI_SCREENS_H */
