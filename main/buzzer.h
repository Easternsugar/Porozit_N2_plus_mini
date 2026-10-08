/**
 * @file buzzer.h
 * @brief sound manager module
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define GPIO_BUZZER 42

/** Highest volume level, the scale runs from 0 to this value */
#define BUZZER_VOLUME_LEVEL_MAX 5

/**
 * @brief Set the buzzer volume
 * @param level 0 to BUZZER_VOLUME_LEVEL_MAX. Level 0 is silent.
 */
void buzzer_set_volume_level(uint8_t level);

/**
 * @brief Configure the LEDC timer and channel used by the buzzer
 *
 * Idempotent. playSound() calls it as a fallback, but doing it once during
 * start-up keeps the first beep from paying for the setup.
 *
 * @return ESP_OK on success
 */
esp_err_t buzzer_init(void);

void playSound(const uint32_t freq, const uint32_t duration_ms, const uint8_t repeat);
void playSoundOnce(const uint32_t freq, const uint32_t duration_ms);

/**
 * @brief Enable or disable all buzzer output.
 * @param enabled true to allow sounds, false to mute.
 */
void buzzer_set_enabled(bool enabled);

/**
 * @brief Query whether the buzzer is currently enabled.
 * @return true if sounds are allowed.
 */
bool buzzer_is_enabled(void);

/** Select one of the five sound levels (0 mutes the output). */
void buzzer_set_volume(uint8_t level);
