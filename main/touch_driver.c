/**
 * @file touch_driver.c
 * @brief Touch screen driver implementation (CST816S)
 */

#include "touch_driver.h"
#include "power_save.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst816s.h"

static const char *TAG = "touch_driver";

/* Touch controller handle */
static esp_lcd_touch_handle_t touch_handle = NULL;
static i2c_master_bus_handle_t touch_i2c_bus = NULL;

/* LVGL input device driver (static for callback) */
static lv_indev_drv_t indev_drv;

/**
 * @brief LVGL touch read callback
 */
static void touch_lvgl_callback(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)drv->user_data;
    if (tp == NULL) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t tp_x;
    uint16_t tp_y;
    uint8_t tp_cnt = 0;

    /* Read data from touch controller */
    esp_lcd_touch_read_data(tp);
    
    /* Get touch coordinates */
    bool tp_pressed = esp_lcd_touch_get_coordinates(tp, &tp_x, &tp_y, NULL, &tp_cnt, 1);
    
    if (tp_pressed && tp_cnt > 0) {
        data->point.x = tp_x;
        data->point.y = tp_y;
        data->state = LV_INDEV_STATE_PRESSED;
        /* Screen interaction is what keeps the watch awake */
        power_save_notify_activity();
        ESP_LOGD(TAG, "Touch position: %d,%d", tp_x, tp_y);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

esp_err_t touch_driver_init(uint16_t lcd_h_res, uint16_t lcd_v_res)
{
    ESP_LOGI(TAG, "Initializing touch driver");

    /* Suppress noisy logs from touch libraries */
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("CST816S", ESP_LOG_NONE);

    /* Configure I2C bus */
    const i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = TOUCH_I2C_HOST,
        .sda_io_num = TOUCH_GPIO_SDA,
        .scl_io_num = TOUCH_GPIO_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&i2c_bus_cfg, &touch_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C master bus init failed");
        return ret;
    }

    /* Create panel IO for touch */
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    tp_io_config.scl_speed_hz = TOUCH_I2C_CLK_HZ;

    ret = esp_lcd_new_panel_io_i2c(touch_i2c_bus, &tp_io_config, &tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO for touch");
        return ret;
    }

    /* Configure touch controller */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = lcd_h_res,
        .y_max = lcd_v_res,
        .rst_gpio_num = TOUCH_GPIO_RST,
        .int_gpio_num = TOUCH_GPIO_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    /* Initialize CST816S touch controller */
    ret = esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize touch controller");
        return ret;
    }

    ESP_LOGI(TAG, "Touch driver initialized successfully");
    return ESP_OK;
}

esp_err_t touch_driver_register_lvgl(lv_disp_t *disp)
{
    if (touch_handle == NULL) {
        ESP_LOGE(TAG, "Touch driver not initialized");
        return ESP_FAIL;
    }

    if (disp == NULL) {
        ESP_LOGE(TAG, "Display handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Registering touch input with LVGL");

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = touch_lvgl_callback;
    indev_drv.user_data = touch_handle;
    lv_indev_drv_register(&indev_drv);

    ESP_LOGI(TAG, "Touch input registered with LVGL");
    return ESP_OK;
}

esp_lcd_touch_handle_t touch_driver_get_handle(void)
{
    return touch_handle;
}
