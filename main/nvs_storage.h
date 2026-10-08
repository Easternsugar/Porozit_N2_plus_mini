/**
 * @file nvs_storage.h
 * @brief NVS manager module
 */

#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Handle NVS operations (read, save)
 * return ESP_OK on success
*/

/** Buffer size for the language code, e.g. "EN" plus the terminating NUL. */
#define SETTINGS_LANG_SIZE 8

/** Language used when nothing has been stored yet. */
#define SETTINGS_DEFAULT_LANG "EN"

/** Beep interval index used when nothing has been stored yet (non-zero = on) */
#define SETTINGS_DEFAULT_BEEP_TIMER 2
/** Auto sleep index used when nothing has been stored yet (0 = 5 minutes) */
#define SETTINGS_DEFAULT_SLEEP_TIMER 0
/* Brightness and volume default to the top of their scale, which is how the
 * watch behaved before they became adjustable. */
#define SETTINGS_DEFAULT_BRIGHTNESS 6
#define SETTINGS_DEFAULT_VOLUME     6

typedef struct{
    bool beep_enabled;
    char lang[SETTINGS_LANG_SIZE];
    uint8_t brightness;
    uint8_t volume;
    /** Index into the beep interval table, 0 means the beep is off */
    uint8_t beep_timer;
    /** Index into the auto sleep interval table */
    uint8_t sleep_timer;
    /** Which quantity the big number shows, 0 = time, 1 = PMA */
    uint8_t measurement_unit;
} settings_t;

//------Settings methodes-------

/**
 * @brief Read every setting, substituting defaults for anything not stored
 *
 * Missing keys or a missing namespace are not treated as errors: the
 * corresponding field is filled with its default and ESP_OK is returned, so
 * the caller can always use the returned struct.
 */
esp_err_t nvs_settings_load(settings_t *settings);

/** @brief Write every setting in one transaction */
esp_err_t nvs_settings_save(const settings_t *settings);

esp_err_t nvs_settings_set_beep(const bool beep_enabled);
esp_err_t nvs_settings_get_beep(bool *beep_enabled);

esp_err_t nvs_settings_set_lang(const char *lang);

/**
 * @brief Read the stored language code
 * @param lang      Destination buffer, filled with the default on any failure
 * @param lang_size Size of the destination buffer, at least SETTINGS_LANG_SIZE
 */
esp_err_t nvs_settings_get_lang(char *lang, size_t lang_size);

/** @brief Store the beep interval index (0 = off) */
esp_err_t nvs_settings_set_beep_timer(uint8_t index);

/** @brief Store the auto sleep interval index */
esp_err_t nvs_settings_set_sleep_timer(uint8_t index);

/** @brief Store the backlight brightness level */
esp_err_t nvs_settings_set_brightness(uint8_t level);

/** @brief Store the buzzer volume level */
esp_err_t nvs_settings_set_volume(uint8_t level);

/** @brief Store which quantity the big number shows (0 = time, 1 = PMA) */
esp_err_t nvs_settings_set_measurement_unit(uint8_t unit);

//------Measure methodes--------
esp_err_t nvs_measurement_save(const float time);
esp_err_t nvs_measurement_load(float *time);
esp_err_t nvs_measurement_clear(void);

#endif /* NVS_STORAGE_H */
