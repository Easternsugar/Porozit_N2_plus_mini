#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/** Restore persisted settings and start the periodic beep scheduler. */
esp_err_t watch_settings_init(void);

/** Validate and apply one {"version":1,"type":"config",field:value} patch. */
bool watch_settings_apply_json(const char *payload, size_t length);
