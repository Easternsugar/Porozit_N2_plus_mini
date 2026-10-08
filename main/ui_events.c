/**
 * @file ui_events.c
 * @brief Implementations of the SquareLine-generated UI event callbacks.
 *
 * SquareLine Studio declares these handlers in ui/ui_events.h but does not
 * generate their bodies. They are implemented here so the custom code is not
 * overwritten when the UI is re-exported from SquareLine.
 */

#include <stdbool.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "ui/ui.h"
#include "buzzer.h"
#include "i18n.h"
#include "nvs_storage.h"

#define TAG "ui_events"

void changeBeep(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    bool enabled = !buzzer_is_enabled();
    buzzer_set_enabled(enabled);

    if (ui_beepValue != NULL) {
        lv_label_set_text(ui_beepValue, i18n(enabled ? STR_ON : STR_OFF));
    }

    nvs_settings_set_beep(enabled);

    /* Give audible feedback only when turning the beep on. */
    if (enabled) {
        playSoundOnce(1200, 150);
    }

    ESP_LOGI(TAG, "Beep %s", enabled ? "enabled" : "disabled");
}

void changeLang(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    /* Steps through EN -> HU -> DE -> ES -> FR -> EN. The call persists the
     * choice and rewrites every translated label, including the value shown in
     * this row, so there is nothing else to update here. */
    lang_id_t lang = i18n_cycle_lang();

    ESP_LOGI(TAG, "Language set to %s", i18n_lang_code(lang));
}
