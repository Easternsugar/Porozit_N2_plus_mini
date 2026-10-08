/**
 * @file i18n.h
 * @brief Localization for the watch UI (EN / HU / DE / ES / FR)
 *
 * All user visible text goes through i18n(): the module keeps one string table
 * per language and hands back the entry for the language that is currently
 * selected. The selected language is persisted in NVS, so it survives a
 * power cycle.
 *
 * This module lives outside the SquareLine generated `ui/` folder so it is not
 * lost when the UI is re-exported.
 */

#ifndef I18N_H
#define I18N_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Supported languages
 *
 * The order is also the order the settings menu cycles through.
 */
typedef enum {
    LANG_EN = 0,
    LANG_HU,
    LANG_DE,
    LANG_ES,
    LANG_FR,
    LANG_COUNT,
} lang_id_t;

/**
 * @brief Every translatable string in the UI
 *
 * Keep this in sync with the tables in i18n.c. An entry that is missing from a
 * language falls back to English instead of rendering as an empty label.
 */
typedef enum {
    STR_SEC,                    /**< Unit next to the measured time */
    STR_DELETE,                 /**< Delete button on the dashboard */
    STR_SAVE,                   /**< Save button on the dashboard */
    STR_BEEP,                   /**< Beep row label in the menu */
    STR_LANGUAGE,               /**< Language row label in the menu */
    STR_ON,                     /**< Beep enabled value */
    STR_OFF,                    /**< Beep disabled value */
    STR_BYE,                    /**< Shutdown message on the splash screen */
    STR_CHARGING,               /**< Charge screen, charging */
    STR_BATTERY_EMPTY,          /**< Charge screen, empty battery */
    STR_ALERT_NO_DEVICE,        /**< Status: probe not plugged in */
    STR_ALERT_MEASURE_ERROR,    /**< Status: measurement failed */
    STR_ALERT_MEASURING,        /**< Status: measurement running */
    STR_ALERT_MEASURE_DONE,     /**< Status: measurement finished */
    STR_ALERT_STANDBY,          /**< Status: idle, ready to measure */
    STR_COUNT,
} str_id_t;

/**
 * @brief Look up a string in the language that is currently selected
 * @return Pointer to a static string, never NULL
 */
const char *i18n(str_id_t id);

/** @brief Currently selected language */
lang_id_t i18n_get_lang(void);

/**
 * @brief Two letter code shown in the settings menu, e.g. "EN"
 * @return Pointer to a static string, never NULL
 */
const char *i18n_lang_code(lang_id_t lang);

/**
 * @brief Resolve a language code back to its id
 * @param code Case insensitive code such as "hu"
 * @return The matching language, or LANG_EN when the code is unknown
 */
lang_id_t i18n_lang_from_code(const char *code);

/**
 * @brief Select a language without touching NVS or the widgets
 *
 * Used at boot, where the value comes from NVS anyway and the labels are
 * refreshed once at the end of the restore.
 */
void i18n_select_lang(lang_id_t lang);

/**
 * @brief Select a language by its code without touching NVS or the widgets
 */
void i18n_select_lang_by_code(const char *code);

/**
 * @brief Select a language, persist it and relabel the UI
 * @return Result of the NVS write; the UI is updated either way
 */
esp_err_t i18n_set_lang(lang_id_t lang);

/**
 * @brief Advance to the next language, persist it and relabel the UI
 * @return The language that is now active
 */
lang_id_t i18n_cycle_lang(void);

/**
 * @brief Write the current language into every widget that shows text
 *
 * Safe to call from any task and from inside an LVGL event callback; it takes
 * the LVGL port mutex, which is recursive.
 */
void i18n_apply_ui(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* I18N_H */
