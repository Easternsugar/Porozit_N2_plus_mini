/**
 * @file nvs_storage.c
 * @brief NVS Storage management implementation
 */

#include "nvs_storage.h"

#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"

#define TAG "storage"

#define NVS_NS_SETTINGS  "settings"
#define NVS_KEY_BEEP     "beep_on"
#define NVS_KEY_LANG     "lang"
#define NVS_KEY_BEEP_TMR "beep_timer"
#define NVS_KEY_SLEEP_TMR "sleep_timer"
#define NVS_KEY_BRIGHTNESS "brightness"
#define NVS_KEY_VOLUME   "volume"
#define NVS_KEY_MEAS_UNIT "meas_unit"

#define NVS_NS_MEASURE   "measure"
#define NVS_KEY_MEASURE  "last_measure"

/**
 * @brief strncpy that always NUL terminates
 *
 * The stored language code is handed straight to lv_label_set_text(), so an
 * unterminated buffer would make LVGL read past the end of the struct.
 */
static void nvs_copy_str(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    strncpy(dst, (src != NULL) ? src : "", dst_size - 1);
    dst[dst_size - 1] = '\0';
}

/**
 * @brief Write a single uint8 setting and commit it
 */
static esp_err_t nvs_settings_set_u8(const char *key, uint8_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS_SETTINGS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(handle, key, value);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_settings_set_beep_timer(uint8_t index)
{
    return nvs_settings_set_u8(NVS_KEY_BEEP_TMR, index);
}

esp_err_t nvs_settings_set_sleep_timer(uint8_t index)
{
    return nvs_settings_set_u8(NVS_KEY_SLEEP_TMR, index);
}

esp_err_t nvs_settings_set_brightness(uint8_t level)
{
    return nvs_settings_set_u8(NVS_KEY_BRIGHTNESS, level);
}

esp_err_t nvs_settings_set_volume(uint8_t level)
{
    return nvs_settings_set_u8(NVS_KEY_VOLUME, level);
}

esp_err_t nvs_settings_set_measurement_unit(uint8_t unit)
{
    return nvs_settings_set_u8(NVS_KEY_MEAS_UNIT, unit);
}

esp_err_t nvs_settings_save(const settings_t *settings){
    //settings save to NVS
    nvs_handle_t handle;
    esp_err_t err;

    if(settings == NULL) return ESP_ERR_INVALID_ARG;

    err = nvs_open(NVS_NS_SETTINGS, NVS_READWRITE, &handle);
    if(err != ESP_OK) return err;

    err = nvs_set_u8(handle, NVS_KEY_BEEP, settings->beep_enabled ? 1 : 0);
    if(err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_LANG, settings->lang);
    if(err != ESP_OK){
        nvs_close(handle);
        return err;
    }

    err = nvs_set_u8(handle, NVS_KEY_BEEP_TMR, settings->beep_timer);
    if(err != ESP_OK){
        nvs_close(handle);
        return err;
    }

    err = nvs_set_u8(handle, NVS_KEY_SLEEP_TMR, settings->sleep_timer);
    if(err != ESP_OK){
        nvs_close(handle);
        return err;
    }

    err = nvs_set_u8(handle, NVS_KEY_BRIGHTNESS, settings->brightness);
    if(err != ESP_OK){
        nvs_close(handle);
        return err;
    }

    err = nvs_set_u8(handle, NVS_KEY_VOLUME, settings->volume);
    if(err != ESP_OK){
        nvs_close(handle);
        return err;
    }

    err = nvs_set_u8(handle, NVS_KEY_MEAS_UNIT, settings->measurement_unit);
    if(err != ESP_OK){
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_settings_load(settings_t *settings){
    //settings load from NVS

    nvs_handle_t handle;
    esp_err_t err;

    if(settings == NULL) return ESP_ERR_INVALID_ARG;

    /* Start from the defaults so every field is valid even when NVS is empty
     * or a single key is missing. */
    settings->beep_enabled = true;
    settings->brightness = SETTINGS_DEFAULT_BRIGHTNESS;
    settings->volume = SETTINGS_DEFAULT_VOLUME;
    settings->beep_timer = SETTINGS_DEFAULT_BEEP_TIMER;
    settings->sleep_timer = SETTINGS_DEFAULT_SLEEP_TIMER;
    /* 0 = the time stays the big number, which is how it has always looked */
    settings->measurement_unit = 0;
    nvs_copy_str(settings->lang, sizeof(settings->lang), SETTINGS_DEFAULT_LANG);

    err = nvs_open(NVS_NS_SETTINGS, NVS_READONLY, &handle);
    if(err != ESP_OK){
        /* Nothing saved yet: the defaults above are the answer. */
        return ESP_OK;
    }

    uint8_t val = 1;
    err = nvs_get_u8(handle, NVS_KEY_BEEP, &val);
    if(err == ESP_OK){
        settings->beep_enabled = (val != 0);
    }

    /* nvs_get_str() needs the real buffer size, not the string length. Passing
     * a size that is too small fails with ESP_ERR_NVS_INVALID_LENGTH. */
    char lang[SETTINGS_LANG_SIZE];
    size_t lang_size = sizeof(lang);
    err = nvs_get_str(handle, NVS_KEY_LANG, lang, &lang_size);
    if(err == ESP_OK){
        nvs_copy_str(settings->lang, sizeof(settings->lang), lang);
    }

    uint8_t index = 0;
    if(nvs_get_u8(handle, NVS_KEY_BEEP_TMR, &index) == ESP_OK){
        settings->beep_timer = index;
    }
    if(nvs_get_u8(handle, NVS_KEY_SLEEP_TMR, &index) == ESP_OK){
        settings->sleep_timer = index;
    }
    if(nvs_get_u8(handle, NVS_KEY_BRIGHTNESS, &index) == ESP_OK){
        settings->brightness = index;
    }
    if(nvs_get_u8(handle, NVS_KEY_VOLUME, &index) == ESP_OK){
        settings->volume = index;
    }
    if(nvs_get_u8(handle, NVS_KEY_MEAS_UNIT, &index) == ESP_OK){
        settings->measurement_unit = index;
    }

    nvs_close(handle);

    /* Missing keys are not a failure, the caller got usable defaults. */
    return ESP_OK;
}

esp_err_t nvs_settings_set_beep(const bool beep_enabled){
    //Beep save to NVS
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NS_SETTINGS, NVS_READWRITE, &handle);
    if(err != ESP_OK) return err;

    /* Same key as nvs_settings_save()/load(), otherwise the toggle is written
     * to a key that nobody reads back at boot. */
    err = nvs_set_u8(handle, NVS_KEY_BEEP, beep_enabled ? 1 : 0);
    if(err != ESP_OK){
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_settings_get_beep(bool *beep_enabled){
    //Beep load from NVS
    nvs_handle_t handle;
    esp_err_t err;

    if(beep_enabled == NULL) return ESP_ERR_INVALID_ARG;

    *beep_enabled = true;

    err = nvs_open(NVS_NS_SETTINGS, NVS_READONLY, &handle);
    if(err != ESP_OK) return err;

    uint8_t val = 1;
    err = nvs_get_u8(handle, NVS_KEY_BEEP, &val);
    if(err != ESP_OK){
        nvs_close(handle);
        return err;
    }

    *beep_enabled = (val != 0);

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t nvs_settings_set_lang(const char *lang){
    //Lang save to NVS
    nvs_handle_t handle;
    esp_err_t err;

    if(lang == NULL) return ESP_ERR_INVALID_ARG;

    err = nvs_open(NVS_NS_SETTINGS, NVS_READWRITE, &handle);
    if(err != ESP_OK) return err;

    err = nvs_set_str(handle, NVS_KEY_LANG, lang);
    if(err != ESP_OK){
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_settings_get_lang(char *lang, size_t lang_size){
    //Lang load from NVS
    nvs_handle_t handle;
    esp_err_t err;

    if(lang == NULL || lang_size == 0) return ESP_ERR_INVALID_ARG;

    /* Always leave a usable value behind, even on every error path below. */
    nvs_copy_str(lang, lang_size, SETTINGS_DEFAULT_LANG);

    err = nvs_open(NVS_NS_SETTINGS, NVS_READONLY, &handle);
    if(err != ESP_OK) return err;

    char lang_t[SETTINGS_LANG_SIZE];
    size_t size = sizeof(lang_t);

    err = nvs_get_str(handle, NVS_KEY_LANG, lang_t, &size);
    if(err != ESP_OK){
        nvs_close(handle);
        return err;
    }

    nvs_copy_str(lang, lang_size, lang_t);

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t nvs_measurement_save(const float time){
    //Measurement save to NVS

    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NS_MEASURE, NVS_READWRITE, &handle);
    if(err != ESP_OK) return err;

    err = nvs_set_blob(handle, NVS_KEY_MEASURE, &time, sizeof(time));
    if(err != ESP_OK){
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_measurement_load(float *time){
    //Measurement load from NVS

    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NS_MEASURE, NVS_READONLY, &handle);
    if(err != ESP_OK){
        *time = 0.0f;
        return ESP_OK;
    }

    size_t size = sizeof(float);
    err = nvs_get_blob(handle, NVS_KEY_MEASURE, time, &size);
    if(err == ESP_ERR_NVS_NOT_FOUND){
        *time = 0.0f;
        err = ESP_OK;
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_measurement_clear(void){
    //Measurement clear from NVS

    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NS_MEASURE, NVS_READWRITE, &handle);
    if(err != ESP_OK) return err;

    const float time = 0.0f;

    err = nvs_set_blob(handle, NVS_KEY_MEASURE, &time, sizeof(float));
    if(err != ESP_OK){
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
