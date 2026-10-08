/**
 * @file touch_driver.h
 * @brief Touch screen driver module (CST816S)
 */

#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "lvgl.h"

/* I2C settings for touch */
#define TOUCH_I2C_HOST      I2C_NUM_0
#define TOUCH_I2C_CLK_HZ    (100 * 1000)

/* Touch pins */
#define TOUCH_GPIO_SCL      (GPIO_NUM_10)
#define TOUCH_GPIO_SDA      (GPIO_NUM_11)
#define TOUCH_GPIO_RST      (GPIO_NUM_13)
#define TOUCH_GPIO_INT      (GPIO_NUM_14)

/**
 * @brief Initialize touch controller hardware (I2C and CST816S)
 * @param lcd_h_res LCD horizontal resolution (for touch calibration)
 * @param lcd_v_res LCD vertical resolution (for touch calibration)
 * @return ESP_OK on success
 */
esp_err_t touch_driver_init(uint16_t lcd_h_res, uint16_t lcd_v_res);

/**
 * @brief Register touch input device with LVGL
 * @param disp LVGL display to attach the touch input to
 * @return ESP_OK on success
 */
esp_err_t touch_driver_register_lvgl(lv_disp_t *disp);

/**
 * @brief Get touch controller handle
 * @return Touch handle or NULL if not initialized
 */
esp_lcd_touch_handle_t touch_driver_get_handle(void);

#endif /* TOUCH_DRIVER_H */
