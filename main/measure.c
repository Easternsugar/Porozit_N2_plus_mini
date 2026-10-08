/**
 * @file measure.c
 * @brief Measurement manager implementation
 */

#include "measure.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ui/ui.h"
#include "ui/ui_helpers.h"

#include "buzzer.h"
#include "i18n.h"
#include "power_save.h"
#include "ble_service.h"
#include "nvs_storage.h"

#define TAG "measure"

#define MEASURE_TASK_STACK       4096
#define MEASURE_TASK_PRIORITY    1
#define MEASURE_QUEUE_LEN        8

#define MEASURE_ACTIVE_LEVEL     0
#define PLUG_ACTIVE_LEVEL        0

#define MEASURE_MIN_TIME_S       0.3f
#define MEASURE_UPDATE_MS        10

#define MEASURE_PMA_FACTOR        7500.0f
#define MEASURE_PMA_MAX           9999.0f
#define MEASURE_COUNT_MAX         25

/* Colour bands for the displayed measurement time. The limits are in tenths of
 * a second so they line up exactly with the one decimal the label shows and
 * there is no float rounding on the boundaries:
 *   black  0.0 s (no measurement)
 *   red    0.1 - 14.0 s
 *   yellow 14.1 - 20.0 s
 *   black  20.1 s and above                                                  */
#define MEASURE_TIME_RED_MAX_TENTHS     140
#define MEASURE_TIME_YELLOW_MAX_TENTHS  200

#define MEASURE_COLOR_RED         0xF60F43u   /* same red as the delete button */
#define MEASURE_COLOR_YELLOW      0xFFCC00u
#define MEASURE_COLOR_BLACK       0x282828u   /* the screen's original colour */
#define MEASURE_COLOR_NONE        0xFFFFFFFFu /* nothing applied yet */

/* Keep this ASCII: it can end up in ui_sec, which uses ui_font_reboto_bold_24,
 * and that font carries no accented or symbol glyphs. */
static const char *MEASURE_PMA_UNIT = "L";

/** Which quantity the big number shows */
static measure_unit_t s_unit = MEASURE_UNIT_TIME;

typedef enum {
	MEASURE_EVT_GPIO,
	MEASURE_EVT_SAVE,
	MEASURE_EVT_DELETE,
	/** The BLE link came up or went away, the Save button has to follow */
	MEASURE_EVT_LINK,
} measure_evt_type_t;

typedef struct {
	measure_evt_type_t type;
	gpio_num_t gpio;
} measure_evt_t;

typedef struct {
	bool plugged;
	bool measuring;
	bool allow_measure;
	bool measure_error;
	bool save_delete_enabled;
	uint32_t meas_count;
	float measure_time_s;
	float last_measure_s;
	int64_t measure_start_us;
	uint32_t last_display_tenths;
	uint32_t time_color;
	char time_buf[16];
	char pma_buf[32];
	char count_buf[12];
} measure_state_t;

static QueueHandle_t s_measure_queue;
static TaskHandle_t s_measure_task;
static measure_state_t s_state;
/* Boolean front end for the same setting, used by watch_settings.c. There is
 * only one piece of state behind both spellings, see measure_set_unit(). */
void measure_set_unit_pma(bool enabled)
{
	measure_set_unit(enabled ? MEASURE_UNIT_PMA : MEASURE_UNIT_TIME);
}

bool measure_is_unit_pma(void)
{
	return measure_get_unit() == MEASURE_UNIT_PMA;
}

static void measure_task(void *param);
static void IRAM_ATTR measure_gpio_isr_handler(void *arg);
static void measure_handle_event(measure_state_t *state, const measure_evt_t *evt, int64_t now_us);
static void measure_handle_gpio(measure_state_t *state, gpio_num_t gpio, int64_t now_us);
static void measure_handle_save(measure_state_t *state, int64_t now_us);
static void measure_handle_delete(measure_state_t *state, int64_t now_us);
static void measure_start(measure_state_t *state, int64_t now_us);
static void measure_finalize(measure_state_t *state, bool error, int64_t now_us);
static void measure_ui_update_time(measure_state_t *state, float time_s);
static void measure_ui_update_count(measure_state_t *state);
static void measure_ui_set_save_delete_enabled(bool enabled);
static void measure_ui_update_alert(measure_state_t *state);

static bool measure_gpio_is_active(gpio_num_t gpio)
{
	return gpio_get_level(gpio) == MEASURE_ACTIVE_LEVEL;
}

/**
 * @brief Colour of the measurement time for the given value
 * @param time_s Time in seconds, as it will be displayed
 */
static uint32_t measure_time_color(float time_s)
{
	int32_t tenths = (int32_t)lroundf(time_s * 10.0f);

	/* 0.0 means "no measurement", keep it neutral */
	if (tenths <= 0) {
		return MEASURE_COLOR_BLACK;
	}
	if (tenths <= MEASURE_TIME_RED_MAX_TENTHS) {
		return MEASURE_COLOR_RED;
	}
	if (tenths <= MEASURE_TIME_YELLOW_MAX_TENTHS) {
		return MEASURE_COLOR_YELLOW;
	}
	return MEASURE_COLOR_BLACK;
}

static bool measure_plugged(void)
{
	return gpio_get_level(GPIO_PLUGGED_IN_PIN) == PLUG_ACTIVE_LEVEL;
}

static void IRAM_ATTR measure_gpio_isr_handler(void *arg)
{
	if (s_measure_queue == NULL) {
		return;
	}

	measure_evt_t evt = {
		.type = MEASURE_EVT_GPIO,
		.gpio = (gpio_num_t)(uintptr_t)arg,
	};

	BaseType_t higher_priority_task_woken = pdFALSE;
	xQueueSendFromISR(s_measure_queue, &evt, &higher_priority_task_woken);
	if (higher_priority_task_woken) {
		portYIELD_FROM_ISR();
	}
}

/**
 * @brief Queue a save or delete request if the current state allows it
 *
 * The enabled check lives here rather than in the event callbacks because
 * LV_STATE_DISABLED only greys a button out: LVGL tests it for keypad input
 * only (lv_indev.c, indev_keypad_proc), so a touch still delivers the click.
 * Without this check a tap on the greyed out Delete button still beeped and
 * wiped the stored measurement.
 */
static bool measure_request(measure_evt_type_t type)
{
	if (s_measure_queue == NULL || !s_state.save_delete_enabled) {
		return false;
	}

	/* Mirrors the greyed out Save button. LV_STATE_DISABLED does not stop a
	 * touch from being delivered, so the BLE condition is enforced here too. */
	if (type == MEASURE_EVT_SAVE && !ble_service_can_notify()) {
		ESP_LOGW(TAG, "Save refused, no phone subscribed to notifications");
		return false;
	}

	if (type == MEASURE_EVT_SAVE) {
		playSoundOnce(1200, 300);
	} else {
		playSoundOnce(800, 500);
	}

	measure_evt_t evt = {
		.type = type,
		.gpio = GPIO_NUM_NC,
	};

	return xQueueSend(s_measure_queue, &evt, 0) == pdTRUE;
}

bool measure_request_save(void)
{
	return measure_request(MEASURE_EVT_SAVE);
}

bool measure_request_delete(void)
{
	return measure_request(MEASURE_EVT_DELETE);
}

void measure_save_event(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
		return;
	}

	measure_request_save();
}

void measure_delete_event(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
		return;
	}

	measure_request_delete();
}

/**
 * @brief Format the PMA value for the big label
 *
 * Only so many characters fit in the 60 px font, so the precision drops as the
 * value grows: two decimals below 1000, one decimal below 10000, none above.
 *
 * The thresholds are nudged by half a unit of the next precision so the choice
 * is made on the value as it will be printed. Comparing against a plain 1000.0f
 * would let 999.999 pick the two decimal form and print "1000.00", which is one
 * character wider than anything else in that branch. As written every branch
 * yields at most 6 characters.
 */
static void measure_format_pma_main(char *buf, size_t size, float pma)
{
	if (pma >= 9999.5f) {
		snprintf(buf, size, "%.0f", (double)pma);
	} else if (pma >= 999.95f) {
		snprintf(buf, size, "%.1f", (double)pma);
	} else {
		snprintf(buf, size, "%.2f", (double)pma);
	}
}

static void measure_ui_update_time(measure_state_t *state, float time_s)
{
	float pma = (time_s <= 0.0f) ? 0.0f : (MEASURE_PMA_FACTOR / time_s);

	if (pma > MEASURE_PMA_MAX) {
		pma = 0.0f;
	}

	char time_txt[12];
	if (time_s < 10000.0f) {
		snprintf(time_txt, sizeof(time_txt), "%.1f", (double)time_s);
	} else {
		snprintf(time_txt, sizeof(time_txt), "%.0f", (double)time_s);
	}

	/* time_buf feeds the big label, pma_buf the small one in the header row.
	 * The selected unit decides which quantity goes where; the unit label next
	 * to the big number follows in measure_refresh_units(). */
	if (s_unit == MEASURE_UNIT_PMA) {
		measure_format_pma_main(state->time_buf, sizeof(state->time_buf), pma);
		snprintf(state->pma_buf, sizeof(state->pma_buf), "%s %s", time_txt, i18n(STR_SEC));
	} else {
		snprintf(state->time_buf, sizeof(state->time_buf), "%s", time_txt);
		/* The header font is small, two decimals always fit there */
		snprintf(state->pma_buf, sizeof(state->pma_buf), "%.2f %s",
				 (double)pma, MEASURE_PMA_UNIT);
	}

	/* Only touch the style when the band actually changes: setting it forces a
	 * style refresh plus an invalidate, and this runs on every tenth. */
	const uint32_t color = measure_time_color(time_s);
	const bool color_changed = (color != state->time_color);
	state->time_color = color;

	lvgl_port_lock(-1);
	if (ui_measurement != NULL) {
		lv_label_set_text_static(ui_measurement, state->time_buf);
	}
	/* ui_sec is not touched here on purpose: measure_refresh_units() owns it,
	 * and rewriting it on every tenth would invalidate the label needlessly. */
	if (ui_pmaNum != NULL) {
		lv_label_set_text_static(ui_pmaNum, state->pma_buf);
	}
	if (color_changed) {
		const lv_color_t text_color = lv_color_hex(color);
		if (ui_measurement != NULL) {
			lv_obj_set_style_text_color(ui_measurement, text_color, LV_PART_MAIN | LV_STATE_DEFAULT);
		}
		if (ui_sec != NULL) {
			lv_obj_set_style_text_color(ui_sec, text_color, LV_PART_MAIN | LV_STATE_DEFAULT);
		}
		if (ui_pmaNum != NULL) {
			lv_obj_set_style_text_color(ui_pmaNum, text_color, LV_PART_MAIN | LV_STATE_DEFAULT);
		}
	}
	lvgl_port_unlock();
}

static void measure_ui_update_count(measure_state_t *state)
{
	snprintf(state->count_buf, sizeof(state->count_buf), "| %u", (unsigned)(state->meas_count + 1));

	lvgl_port_lock(-1);
	if (ui_measCount != NULL) {
		lv_label_set_text_static(ui_measCount, state->count_buf);
	}
	lvgl_port_unlock();
}

/**
 * @brief Update both action buttons
 *
 * Saving pushes the measurement to the phone, so without a usable BLE link
 * there is nothing the Save button could do and it stays disabled. Delete only
 * depends on there being a measurement, so it works offline.
 */
static void measure_ui_set_save_delete_enabled(bool enabled)
{
	const bool save_enabled = enabled && ble_service_can_notify();

	lvgl_port_lock(-1);
	if (ui_saveBtn != NULL) {
		_ui_state_modify(ui_saveBtn, LV_STATE_DISABLED,
						 save_enabled ? _UI_MODIFY_STATE_REMOVE : _UI_MODIFY_STATE_ADD);
	}
	if (ui_delBtn != NULL) {
		_ui_state_modify(ui_delBtn, LV_STATE_DISABLED,
						 enabled ? _UI_MODIFY_STATE_REMOVE : _UI_MODIFY_STATE_ADD);
	}
	lvgl_port_unlock();
}

void measure_refresh_save_state(void)
{
	if (s_measure_queue == NULL) {
		return;
	}

	/* Called from the NimBLE host task, which must not block on the LVGL lock,
	 * so the widget work is handed to the measurement task. */
	measure_evt_t evt = {
		.type = MEASURE_EVT_LINK,
		.gpio = GPIO_NUM_NC,
	};
	xQueueSend(s_measure_queue, &evt, 0);
}

static void measure_ui_update_alert(measure_state_t *state)
{
	/* Decide the status message based on the current measurement state.
	   Order matters: errors and "not plugged" take precedence over
	   transient states like measuring/done. */
	const char *msg;
	lv_color_t color;

	if (!state->plugged) {
		msg = i18n(STR_ALERT_NO_DEVICE);
		color = lv_color_hex(0xFF3B30);   /* red */
	} else if (state->measure_error) {
		msg = i18n(STR_ALERT_MEASURE_ERROR);
		color = lv_color_hex(0xFF3B30);   /* red */
	} else if (state->measuring) {
		msg = i18n(STR_ALERT_MEASURING);
		color = lv_color_hex(0xFFCC00);   /* amber */
	} else if (state->last_measure_s > 0.0f) {
		msg = i18n(STR_ALERT_MEASURE_DONE);
		color = lv_color_hex(0x34C759);   /* green */
	} else {
		msg = i18n(STR_ALERT_STANDBY);
		color = lv_color_hex(0xFFFFFF);   /* white */
	}

	lvgl_port_lock(-1);
	if (ui_alert != NULL) {
		lv_label_set_text(ui_alert, msg);
		lv_obj_set_style_text_color(ui_alert, color, LV_PART_MAIN | LV_STATE_DEFAULT);
	}
	lvgl_port_unlock();
}

void measure_refresh_units(void)
{
	/* The unit next to the big number: either the translated "sec" or the PMA
	 * unit, whichever quantity is currently in the big label. */
	lvgl_port_lock(-1);
	if (ui_sec != NULL) {
		lv_label_set_text(ui_sec, (s_unit == MEASURE_UNIT_PMA) ? MEASURE_PMA_UNIT
															   : i18n(STR_SEC));
	}
	lvgl_port_unlock();
}

measure_unit_t measure_get_unit(void)
{
	return s_unit;
}

const char *measure_unit_name(measure_unit_t unit)
{
	return (unit == MEASURE_UNIT_PMA) ? "pma" : "sec";
}

bool measure_unit_from_name(const char *name, measure_unit_t *out)
{
	if (name == NULL || out == NULL) {
		return false;
	}

	if (strcasecmp(name, "sec") == 0 || strcasecmp(name, "s") == 0 ||
		strcasecmp(name, "time") == 0) {
		*out = MEASURE_UNIT_TIME;
		return true;
	}

	if (strcasecmp(name, "pma") == 0 || strcasecmp(name, "l/m2/min") == 0 ||
		strcasecmp(name, MEASURE_PMA_UNIT) == 0) {
		*out = MEASURE_UNIT_PMA;
		return true;
	}

	return false;
}

void measure_set_unit(measure_unit_t unit)
{
	s_unit = (unit == MEASURE_UNIT_PMA) ? MEASURE_UNIT_PMA : MEASURE_UNIT_TIME;

	measure_refresh_units();

	/* Redraw the numbers so they land on the right side of the swap. The last
	 * finished measurement is shown, 0 when there is none. */
	measure_ui_update_time(&s_state, (s_state.last_measure_s > 0.0f)
									 ? s_state.last_measure_s : 0.0f);

	ESP_LOGI(TAG, "Unit set to %s", measure_unit_name(s_unit));
}

void measure_refresh_alert(void)
{
	if (s_measure_task == NULL) {
		/* Not initialized yet, measure_init() emits the alert on its own. */
		return;
	}

	measure_ui_update_alert(&s_state);
}

static void measure_start(measure_state_t *state, int64_t now_us)
{
	state->measuring = true;
	state->measure_error = false;
	state->measure_start_us = now_us;
	state->measure_time_s = 0.0f;
	state->last_display_tenths = UINT32_MAX;

	if (state->save_delete_enabled) {
		state->save_delete_enabled = false;
		measure_ui_set_save_delete_enabled(false);
	}

	char json[96];
	int64_t timestamp_ms = now_us / 1000;
	int len = snprintf(json, sizeof(json),
					   "{\"version\":1, \"type\":\"measurement\", \"alert\":\"started\", \"timestamp\":%lld}",
					   (long long)timestamp_ms);
	if (len > 0 && (size_t)len < sizeof(json)) {
		esp_err_t err = ble_service_notify((const uint8_t *)json, (size_t)len);
		if (err == ESP_OK) {
			ESP_LOGI(TAG, "Measurement sent via BLE: %s", json);
		} else {
			ESP_LOGW(TAG, "BLE notify failed: %s", esp_err_to_name(err));
		}
	}

	measure_ui_update_time(state, 0.0f);
	ESP_LOGI(TAG, "Measurement started");

	measure_ui_update_alert(state);

	playSoundOnce(1200, 300);
}

static void measure_finalize(measure_state_t *state, bool error, int64_t now_us)
{
	if (!state->measuring) {
		return;
	}

	float elapsed = (float)(now_us - state->measure_start_us) / 1000000.0f;

	state->measure_time_s = elapsed;
	state->measuring = false;
	state->measure_error = error;

	if (!error && elapsed > MEASURE_MIN_TIME_S) {
		state->last_measure_s = elapsed;
		state->allow_measure = false;
		if (!state->save_delete_enabled) {
			state->save_delete_enabled = true;
			measure_ui_set_save_delete_enabled(true);
		}
	} else {
		state->last_measure_s = 0.0f;
		state->allow_measure = error ? false : true;
		if (state->save_delete_enabled) {
			state->save_delete_enabled = false;
			measure_ui_set_save_delete_enabled(false);
		}
	}

	measure_ui_update_time(state, state->last_measure_s > 0.0f ? state->last_measure_s : 0.0f);

	nvs_measurement_save((const float)state->last_measure_s);

	char json[120];
	int64_t timestamp_ms = now_us / 1000;
	int len = snprintf(json, sizeof(json),
					   "{\"version\":1, \"type\":\"measurement\", \"alert\":\"done\", \"time\":\"%.6f\", \"timestamp\":%lld}",
					   (double)state->last_measure_s,
					   (long long)timestamp_ms);
	if (len > 0 && (size_t)len < sizeof(json)) {
		esp_err_t err = ble_service_notify((const uint8_t *)json, (size_t)len);
		if (err == ESP_OK) {
			ESP_LOGI(TAG, "Measurement sent via BLE: %s", json);
		} else {
			ESP_LOGW(TAG, "BLE notify failed: %s", esp_err_to_name(err));
		}
	}

	ESP_LOGI(TAG, "Measurement %s (%.3f s)", error ? "error" : "done", (double)elapsed);

	measure_ui_update_alert(state);

	playSound(1200, 300, 2);
}

static void measure_handle_gpio(measure_state_t *state, gpio_num_t gpio, int64_t now_us)
{
	if (gpio == GPIO_PLUGGED_IN_PIN) {
		const bool plugged = measure_plugged();
		if (plugged == state->plugged) {
			return;
		}

		state->plugged = plugged;
		if (plugged) {
			playSoundOnce(1200, 300);
			state->measure_error = false;
			if (!state->measuring && !state->allow_measure && state->last_measure_s <= 0.0f) {
				state->allow_measure = true;
			}
			if (state->allow_measure && measure_gpio_is_active(GPIO_MEASURE_PIN)) {
				measure_start(state, now_us);
			}
		} else {
			playSoundOnce(800, 500);
			state->allow_measure = false;
			if (state->measuring) {
				float elapsed = (float)(now_us - state->measure_start_us) / 1000000.0f;
				measure_finalize(state, elapsed > MEASURE_MIN_TIME_S, now_us);
			}
		}
		measure_ui_update_alert(state);
		return;
	}

	if (gpio == GPIO_MEASURE_PIN) {
		if (measure_gpio_is_active(GPIO_MEASURE_PIN) && state->plugged && state->allow_measure && !state->measuring) {
			measure_start(state, now_us);
		}
	}
}

static void measure_handle_save(measure_state_t *state, int64_t now_us)
{
	if (state->measuring || state->allow_measure || state->last_measure_s <= 0.0f) {
		return;
	}

	/* Send measurement time via BLE before resetting state */
	char json[96];
	int64_t timestamp_ms = now_us / 1000;
	int len = snprintf(json, sizeof(json),
					   "{\"version\":1, \"type\":\"measurement\", \"alert\":\"save\", \"time\":%.6f, \"timestamp\":%lld}",
					   (double)state->last_measure_s,
					   (long long)timestamp_ms);
	if (len > 0 && (size_t)len < sizeof(json)) {
		esp_err_t err = ble_service_notify((const uint8_t *)json, (size_t)len);
		if (err == ESP_OK) {
			ESP_LOGI(TAG, "Measurement sent via BLE: %s", json);
		} else {
			ESP_LOGW(TAG, "BLE notify failed: %s", esp_err_to_name(err));
		}
	}

	if (state->meas_count < MEASURE_COUNT_MAX) {
		state->meas_count++;
	}

	state->last_measure_s = 0.0f;
	state->measure_error = false;
	state->allow_measure = true;
	state->measure_time_s = 0.0f;
	state->last_display_tenths = UINT32_MAX;

	measure_ui_update_time(state, 0.0f);
	measure_ui_update_count(state);
	if (state->save_delete_enabled) {
		state->save_delete_enabled = false;
		measure_ui_set_save_delete_enabled(false);
	}

	nvs_measurement_clear();

	measure_ui_update_alert(state);

	if (state->plugged && measure_gpio_is_active(GPIO_MEASURE_PIN)) {
		measure_start(state, now_us);
	}

	ESP_LOGI(TAG, "Measurement saved (count=%u)", (unsigned)state->meas_count);
}

static void measure_handle_delete(measure_state_t *state, int64_t now_us)
{
	if (state->measuring || state->allow_measure) {
		return;
	}

	/* Moved here from measure_delete_event() so the stored value is only wiped
	 * once the state machine has agreed that deleting is allowed. */
	nvs_measurement_clear();

	state->last_measure_s = 0.0f;
	state->measure_error = false;
	state->allow_measure = true;
	state->measure_time_s = 0.0f;
	state->last_display_tenths = UINT32_MAX;

	measure_ui_update_time(state, 0.0f);
	if (state->save_delete_enabled) {
		state->save_delete_enabled = false;
		measure_ui_set_save_delete_enabled(false);
	}

	char json[96];
	int64_t timestamp_ms = now_us / 1000;
	int len = snprintf(json, sizeof(json),
					   "{\"version\":1, \"type\":\"measurement\", \"alert\":\"delete\", \"timestamp\":%lld}",
					   (long long)timestamp_ms);
	if (len > 0 && (size_t)len < sizeof(json)) {
		esp_err_t err = ble_service_notify((const uint8_t *)json, (size_t)len);
		if (err == ESP_OK) {
			ESP_LOGI(TAG, "Measurement sent via BLE: %s", json);
		} else {
			ESP_LOGW(TAG, "BLE notify failed: %s", esp_err_to_name(err));
		}
	}

	measure_ui_update_alert(state);

	if (state->plugged && measure_gpio_is_active(GPIO_MEASURE_PIN)) {
		measure_start(state, now_us);
	}

	ESP_LOGI(TAG, "Measurement deleted");
}

static void measure_handle_event(measure_state_t *state, const measure_evt_t *evt, int64_t now_us)
{
	/* Plugging in the probe, measuring, saving and deleting are all user
	 * interaction, so they must wake the watch up and keep it awake. A BLE link
	 * change is not, which is why the notify sits inside the cases. */
	switch (evt->type) {
	case MEASURE_EVT_GPIO:
		power_save_notify_activity();
		measure_handle_gpio(state, evt->gpio, now_us);
		break;
	case MEASURE_EVT_SAVE:
		power_save_notify_activity();
		measure_handle_save(state, now_us);
		break;
	case MEASURE_EVT_DELETE:
		power_save_notify_activity();
		measure_handle_delete(state, now_us);
		break;
	case MEASURE_EVT_LINK:
		measure_ui_set_save_delete_enabled(state->save_delete_enabled);
		break;
	default:
		break;
	}
}

static void measure_task(void *param)
{
	(void)param;

	ESP_LOGI(TAG, "Measurement task started");

	while (1) {
		measure_evt_t evt;
		TickType_t wait_ticks = s_state.measuring ? pdMS_TO_TICKS(MEASURE_UPDATE_MS) : portMAX_DELAY;

		if (xQueueReceive(s_measure_queue, &evt, wait_ticks) == pdTRUE) {
			int64_t now_us = esp_timer_get_time();
			measure_handle_event(&s_state, &evt, now_us);
		}

		if (s_state.measuring) {
			/* Never let the screen go dark in the middle of a measurement */
			power_save_notify_activity();

			int64_t now_us = esp_timer_get_time();
			float elapsed = (float)(now_us - s_state.measure_start_us) / 1000000.0f;
			s_state.measure_time_s = elapsed;

			uint32_t tenths = (uint32_t)(elapsed * 10.0f + 0.5f);
			if (tenths != s_state.last_display_tenths) {
				s_state.last_display_tenths = tenths;
				measure_ui_update_time(&s_state, (float)tenths / 10.0f);
			}

			if (!measure_gpio_is_active(GPIO_MEASURE_PIN) && elapsed > MEASURE_MIN_TIME_S) {
				measure_finalize(&s_state, false, now_us);
			}
		}
	}
}

esp_err_t measure_init(void)
{
	if (s_measure_task != NULL) {
		return ESP_OK;
	}

	gpio_config_t cfg = {
		.pin_bit_mask = (1ULL << GPIO_MEASURE_PIN) | (1ULL << GPIO_PLUGGED_IN_PIN),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_ANYEDGE,
	};

	esp_err_t ret = gpio_config(&cfg);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
		return ret;
	}

	ret = gpio_install_isr_service(0);
	if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "ISR service install failed: %s", esp_err_to_name(ret));
		return ret;
	}

	ret = gpio_isr_handler_add(GPIO_MEASURE_PIN, measure_gpio_isr_handler, (void *)GPIO_MEASURE_PIN);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Measure ISR add failed: %s", esp_err_to_name(ret));
		return ret;
	}

	ret = gpio_isr_handler_add(GPIO_PLUGGED_IN_PIN, measure_gpio_isr_handler, (void *)GPIO_PLUGGED_IN_PIN);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Plug ISR add failed: %s", esp_err_to_name(ret));
		return ret;
	}

	s_measure_queue = xQueueCreate(MEASURE_QUEUE_LEN, sizeof(measure_evt_t));
	if (s_measure_queue == NULL) {
		ESP_LOGE(TAG, "Failed to create measure queue");
		return ESP_ERR_NO_MEM;
	}

	s_state = (measure_state_t) {
		.plugged = measure_plugged(),
		.measuring = false,
		.allow_measure = true,
		.measure_error = false,
		.save_delete_enabled = false,
		.meas_count = 0,
		.measure_time_s = 0.0f,
		.last_measure_s = 0.0f,
		.measure_start_us = 0,
		.last_display_tenths = UINT32_MAX,
		.time_color = MEASURE_COLOR_NONE,
	};

	// lvgl_port_lock(-1);
	// if (ui_saveBtn != NULL) {
	// 	lv_obj_add_event_cb(ui_saveBtn, measure_save_event, LV_EVENT_CLICKED, NULL);
	// }
	// if (ui_delBtn != NULL) {
	// 	lv_obj_add_event_cb(ui_delBtn, measure_delete_event, LV_EVENT_CLICKED, NULL);
	// }
	// lvgl_port_unlock();

	measure_ui_update_count(&s_state);
	measure_ui_update_time(&s_state, 0.0f);
	measure_ui_set_save_delete_enabled(false);

	/* Make child objects of save/delete panels non-clickable so touch events
	   propagate to the parent panel (which has the event callback). */
	lvgl_port_lock(-1);
	if (ui_delImg != NULL) lv_obj_clear_flag(ui_delImg, LV_OBJ_FLAG_CLICKABLE);
	if (ui_delLbl != NULL) lv_obj_clear_flag(ui_delLbl, LV_OBJ_FLAG_CLICKABLE);
	if (ui_saveImg != NULL) lv_obj_clear_flag(ui_saveImg, LV_OBJ_FLAG_CLICKABLE);
	if (ui_saveLbl != NULL) lv_obj_clear_flag(ui_saveLbl, LV_OBJ_FLAG_CLICKABLE);
	lvgl_port_unlock();

	if (s_state.plugged && measure_gpio_is_active(GPIO_MEASURE_PIN)) {
		measure_start(&s_state, esp_timer_get_time());
	}

	BaseType_t task_ok = xTaskCreate(measure_task, "measure_task",
									 MEASURE_TASK_STACK, NULL,
									 MEASURE_TASK_PRIORITY, &s_measure_task);
	if (task_ok != pdPASS) {
		ESP_LOGE(TAG, "Failed to create measurement task");
		return ESP_FAIL;
	}

	float saved_time = 0.0f;
	if(nvs_measurement_load(&saved_time) == ESP_OK && saved_time > 0.0f){
		s_state.last_measure_s = saved_time;
		s_state.allow_measure = false;
		s_state.save_delete_enabled = true;
		measure_ui_update_time(&s_state, saved_time);
		measure_ui_set_save_delete_enabled(true);
	}

	measure_ui_update_alert(&s_state);

	ESP_LOGI(TAG, "Measurement manager initialized");
	return ESP_OK;
}
