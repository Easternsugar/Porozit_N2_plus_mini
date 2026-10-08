#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"

/** Restore persisted settings and start the periodic beep scheduler. */
esp_err_t watch_settings_init(void);

/**
 * Validate and apply the keys of a protocol v2 "config" message (unit, beep, sleep,
 * volume, brightness, language). All or nothing: on failure nothing changes and
 * @p error points to a static, human readable reason.
 */
bool watch_settings_apply(const cJSON *root, const char **error);

/** Write the current settings as a protocol v2 "config" message. Returns snprintf's result. */
int watch_settings_config_json(char *buf, size_t size);
