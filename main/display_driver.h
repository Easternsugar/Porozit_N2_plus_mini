/**
 * @file display_driver.h
 * @brief LCD display and LVGL initialization module
 */

#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"
#include <stdint.h>

/* LCD size */
#define LCD_H_RES           (240)
#define LCD_V_RES           (280)

/* LCD settings */
#define LCD_SPI_NUM         (SPI2_HOST)
#define LCD_PIXEL_CLK_HZ    (40 * 1000 * 1000)
#define LCD_CMD_BITS        (8)
#define LCD_PARAM_BITS      (8)
#define LCD_RGB_ELE_ORDER   (LCD_RGB_ELEMENT_ORDER_RGB)
#define LCD_BITS_PER_PIXEL  (16)
#define LCD_DRAW_BUFF_DOUBLE (1)
#define LCD_DRAW_BUFF_HEIGHT (50)
#define LCD_BL_ON_LEVEL     (1)

/** Highest brightness level, the scale runs from 0 to this value */
#define DISPLAY_BRIGHTNESS_LEVEL_MAX 5

/* LCD pins */
#define LCD_GPIO_SCLK       (GPIO_NUM_6)
#define LCD_GPIO_MOSI       (GPIO_NUM_7)
#define LCD_GPIO_RST        (GPIO_NUM_8)
#define LCD_GPIO_DC         (GPIO_NUM_4)
#define LCD_GPIO_CS         (GPIO_NUM_5)
#define LCD_GPIO_BL         (GPIO_NUM_15)

/**
 * @brief Initialize LCD hardware (SPI, panel, backlight)
 * @return ESP_OK on success
 */
esp_err_t display_lcd_init(void);

/**
 * @brief Initialize LVGL graphics library and add display
 * @return ESP_OK on success
 */
esp_err_t display_lvgl_init(void);

/**
 * @brief Get the LVGL display handle
 * @return Pointer to lv_disp_t or NULL if not initialized
 */
lv_disp_t* display_get_lvgl_display(void);

/**
 * @brief Get LCD IO handle (needed for touch driver)
 * @return LCD panel IO handle
 */
esp_lcd_panel_io_handle_t display_get_lcd_io(void);

/**
 * @brief Get LCD panel handle
 * @return LCD panel handle
 */
esp_lcd_panel_handle_t display_get_lcd_panel(void);

/**
 * @brief Turn backlight on or off
 *
 * Turning it on restores the level set by display_set_brightness_level(), so
 * the idle sleep logic does not need to know about brightness at all.
 *
 * @param on true to turn on, false to turn off
 */
void display_set_backlight(bool on);

/**
 * @brief Set the backlight brightness
 *
 * Takes effect immediately when the backlight is on, otherwise it is applied
 * the next time it is switched on.
 *
 * @param level 0 to DISPLAY_BRIGHTNESS_LEVEL_MAX, higher is brighter. Level 0
 *              is the dimmest readable setting, not off.
 */
void display_set_brightness_level(uint8_t level);

#endif /* DISPLAY_DRIVER_H */
