/**
 * @file buzzer.c
 * @brief Sound manager implementation
 */

#include "buzzer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/ledc.h"

#define TAG "Buzzer"

#define BUZZER_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER       LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BUZZER_DUTY_RES         LEDC_TIMER_10_BIT
#define BUZZER_DUTY_ON          512     /* 50% at 10 bit resolution */
#define BUZZER_DUTY_OFF         0
#define BUZZER_DEFAULT_FREQ_HZ  1200
#define BUZZER_GAP_MS           100

/* 10 bit duty per volume level. A piezo is loudest around 50% duty and gets
 * quieter both below and above it, so BUZZER_DUTY_ON is the top of the usable
 * range. Level 0 is silent. */
static const uint16_t volume_duty[BUZZER_VOLUME_LEVEL_MAX + 1] = {
    0, 102, 205, 307, 410, BUZZER_DUTY_ON
};

static bool s_buzzer_enabled = true;
static bool s_ledc_ready;
static uint16_t s_duty_on = BUZZER_DUTY_ON;

void buzzer_set_enabled(bool enabled){
    s_buzzer_enabled = enabled;
}

bool buzzer_is_enabled(void){
    return s_buzzer_enabled;
}

void buzzer_set_volume_level(uint8_t level)
{
    if (level > BUZZER_VOLUME_LEVEL_MAX) {
        level = BUZZER_VOLUME_LEVEL_MAX;
    }

    s_duty_on = volume_duty[level];

    ESP_LOGI(TAG, "Volume level %u (duty %u/%u)", (unsigned)level,
             (unsigned)s_duty_on, (unsigned)BUZZER_DUTY_ON);
}

esp_err_t buzzer_init(void)
{
    if (s_ledc_ready) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_conf = {
        .speed_mode = BUZZER_LEDC_MODE,
        .duty_resolution = BUZZER_DUTY_RES,
        .timer_num = BUZZER_LEDC_TIMER,
        .freq_hz = BUZZER_DEFAULT_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t ledc_conf = {
        .gpio_num = GPIO_BUZZER,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = BUZZER_DUTY_OFF,
        .hpoint = 0,
    };

    err = ledc_channel_config(&ledc_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ledc_ready = true;
    ESP_LOGI(TAG, "Buzzer ready on GPIO%d", GPIO_BUZZER);
    return ESP_OK;
}

static void buzzer_set_duty(const uint32_t duty)
{
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, duty);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

void playSound(const uint32_t freq, const uint32_t duration_ms, const uint8_t repeat){
    /* Volume level 0 is silence, same end result as the beep being disabled */
    if(!s_buzzer_enabled || repeat == 0 || s_duty_on == 0){
        return;
    }

    /* Fallback for callers that run before buzzer_init() */
    if (buzzer_init() != ESP_OK) {
        return;
    }

    /* Only the frequency changes per sound. Reconfiguring the channel here
     * would re-reserve GPIO_BUZZER and make the LEDC driver warn
     * "GPIO 42 is not usable, maybe conflict with others" on every beep. */
    esp_err_t err = ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cannot set %u Hz: %s", (unsigned)freq, esp_err_to_name(err));
        return;
    }

    for(uint8_t i = 0; i < repeat; i++){
        buzzer_set_duty(s_duty_on);
        vTaskDelay(pdMS_TO_TICKS(duration_ms));

        buzzer_set_duty(BUZZER_DUTY_OFF);
        vTaskDelay(pdMS_TO_TICKS(BUZZER_GAP_MS));
    }
}

void playSoundOnce(const uint32_t freq, const uint32_t duration_ms){
    playSound(freq, duration_ms, 1);
}
