/**
 * @file display_driver.c
 * @brief LCD display and LVGL initialization implementation
 */

#include "display_driver.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"

static const char *TAG = "display_driver";

/* The buzzer owns LEDC timer 0 and channel 0, so the backlight takes 1 and 1.
 * 5 kHz is well above anything audible from the panel. */
#define LCD_BL_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define LCD_BL_LEDC_TIMER   LEDC_TIMER_1
#define LCD_BL_LEDC_CHANNEL LEDC_CHANNEL_1
#define LCD_BL_LEDC_RES     LEDC_TIMER_8_BIT
#define LCD_BL_LEDC_FREQ_HZ 5000
#define LCD_BL_DUTY_MAX     255

/* 8 bit duty per brightness level, roughly geometric so the steps feel evenly
 * spaced to the eye. Level 0 is the dimmest still readable setting rather than
 * off: a brightness of 0 that blanks the screen is indistinguishable from a
 * broken watch. Switching the backlight off is display_set_backlight(false). */
static const uint8_t brightness_duty[DISPLAY_BRIGHTNESS_LEVEL_MAX + 1] = {
    25, 51, 102, 153, 204, LCD_BL_DUTY_MAX
};

static uint8_t backlight_duty = LCD_BL_DUTY_MAX;
static bool backlight_on = false;

/* LCD IO and panel handles */
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;

/* LVGL display handle */
static lv_disp_t *lvgl_disp = NULL;
esp_err_t display_lcd_init(void)
{
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Initializing LCD display");

    /* LCD backlight on LEDC so it can be dimmed, not just switched */
    const ledc_timer_config_t bl_timer = {
        .speed_mode = LCD_BL_LEDC_MODE,
        .duty_resolution = LCD_BL_LEDC_RES,
        .timer_num = LCD_BL_LEDC_TIMER,
        .freq_hz = LCD_BL_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&bl_timer));

    const ledc_channel_config_t bl_channel = {
        .gpio_num = LCD_GPIO_BL,
        .speed_mode = LCD_BL_LEDC_MODE,
        .channel = LCD_BL_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LCD_BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&bl_channel));

    /* Initialize SPI bus */
    ESP_LOGD(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_GPIO_SCLK,
        .mosi_io_num = LCD_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO), TAG, "SPI init failed");

    /* Install panel IO */
    ESP_LOGD(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_GPIO_DC,
        .cs_gpio_num = LCD_GPIO_CS,
        .pclk_hz = LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_NUM, &io_config, &lcd_io), err, TAG, "New panel IO failed");

    /* Install LCD driver (ST7789) */
    ESP_LOGD(TAG, "Install LCD driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_GPIO_RST,
        .rgb_ele_order = LCD_RGB_ELE_ORDER,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7789(lcd_io, &panel_config, &lcd_panel), err, TAG, "New panel failed");

    /* Configure panel */
    esp_lcd_panel_reset(lcd_panel);
    esp_lcd_panel_init(lcd_panel);
    esp_lcd_panel_mirror(lcd_panel, true, true);
    esp_lcd_panel_disp_on_off(lcd_panel, true);
    esp_lcd_panel_set_gap(lcd_panel, 0, 20);
    esp_lcd_panel_invert_color(lcd_panel, true);

    /* Turn backlight on */
    display_set_backlight(true);

    ESP_LOGI(TAG, "LCD display initialized successfully");
    return ret;

err:
    if (lcd_panel) {
        esp_lcd_panel_del(lcd_panel);
        lcd_panel = NULL;
    }
    if (lcd_io) {
        esp_lcd_panel_io_del(lcd_io);
        lcd_io = NULL;
    }
    spi_bus_free(LCD_SPI_NUM);
    return ret;
}

esp_err_t display_lvgl_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL");

    /* Initialize LVGL port */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 5,
        .task_stack = 8192,
        .task_affinity = -1,
        .task_max_sleep_ms = 30,
        .timer_period_ms = 1
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port initialization failed");

    /* Add LCD display to LVGL */
    ESP_LOGD(TAG, "Add LCD screen to LVGL");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT * sizeof(uint16_t),
        .double_buffer = LCD_DRAW_BUFF_DOUBLE,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
        }
    };
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    if (lvgl_disp == NULL) {
        ESP_LOGE(TAG, "Failed to add display to LVGL");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL initialized successfully");
    return ESP_OK;
}

lv_disp_t* display_get_lvgl_display(void)
{
    return lvgl_disp;
}

esp_lcd_panel_io_handle_t display_get_lcd_io(void)
{
    return lcd_io;
}

esp_lcd_panel_handle_t display_get_lcd_panel(void)
{
    return lcd_panel;
}

/**
 * @brief Push a duty value to the backlight channel
 * @note  LCD_BL_ON_LEVEL is 1, so a larger duty means brighter.
 */
static void display_backlight_apply(uint8_t duty)
{
    ledc_set_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL, duty);
    ledc_update_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL);
}

void display_set_backlight(bool on)
{
    backlight_on = on;
    display_backlight_apply(on ? backlight_duty : 0);
    ESP_LOGD(TAG, "Backlight %s", on ? "on" : "off");
}

void display_set_brightness_level(uint8_t level)
{
    if (level > DISPLAY_BRIGHTNESS_LEVEL_MAX) {
        level = DISPLAY_BRIGHTNESS_LEVEL_MAX;
    }

    backlight_duty = brightness_duty[level];

    ESP_LOGI(TAG, "Brightness level %u (duty %u/%u)", (unsigned)level,
             (unsigned)backlight_duty, (unsigned)LCD_BL_DUTY_MAX);

    /* While asleep only remember it, display_set_backlight(true) applies it */
    if (backlight_on) {
        display_backlight_apply(backlight_duty);
    }
}
