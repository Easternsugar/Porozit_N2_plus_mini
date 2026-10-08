set(UI_FILELIST "${CMAKE_CURRENT_LIST_DIR}/ui/filelist.txt")
set(UI_SOURCES)

if(EXISTS "${UI_FILELIST}")
    file(STRINGS "${UI_FILELIST}" UI_SOURCES)
    list(TRANSFORM UI_SOURCES PREPEND "ui/")
    # Re-run CMake when SquareLine re-exports the UI and adds/removes files,
    # otherwise the stale source list causes undefined reference link errors.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${UI_FILELIST}")
else()
    # Fallback for first-time build before SquareLine export creates filelist.txt
    file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS "ui/*.c")
endif()

idf_component_register(SRCS "watch_settings.c" "ble_protocol.c" "battery_ui.c" "i18n.c" "nvs_storage.c" "measure.c" "buzzer.c" "battery_monitor.c" "touch_driver.c" "display_driver.c" "power_manager.c" "power_save.c" "ble_service.c" "ui_events.c" "ui_screens.c" "main.c"
                       ${UI_SOURCES}
                       # cJSON is NOT the old "json" component: ESP-IDF 6.0 moved it out of the
                       # core tree into the registry, so it comes from idf_component.yml
                       # (espressif/cjson) and its CMake name is espressif__cjson.
                       PRIV_REQUIRES lvgl esp_lvgl_port esp_app_format spi_flash esp_adc esp_driver_ledc esp_driver_i2c esp_lcd nvs_flash bt espressif__cjson
                       INCLUDE_DIRS "." "ui")
