# Vendored components

| Component | Version | Source |
|---|---|---|
| `lvgl` | 8.4.0 (`src/`, `Kconfig`, CMake support only) | https://github.com/lvgl/lvgl/tree/v8.4.0 — MIT |
| `esp_lvgl_port` | 2.9.0 (without examples/tests and its `idf_component.yml`) | https://github.com/espressif/esp-bsp/tree/master/components/esp_lvgl_port — Apache-2.0 |

Why in the repository instead of `idf_component.yml`: with ESP-IDF 6 the component manager stops at
configure ("Missing required kconfig option") on the registry's `lvgl/lvgl` 8.4.0 package, whose
dependency rules refer to LVGL 9 options. Keeping LVGL 8 here as a local component avoids the
registry package altogether. `esp_lvgl_port` comes along because its registry manifest would pull
`lvgl/lvgl` back in. The top-level `CMakeLists.txt` sets `LVGL_VERSION`, which `esp_lvgl_port`
needs to pick its LVGL 8 port when LVGL is a local component.
