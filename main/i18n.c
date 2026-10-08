/**
 * @file i18n.c
 * @brief Localization tables and UI refresh (see i18n.h)
 *
 * FONT COVERAGE, PLEASE READ BEFORE ADDING TRANSLATIONS
 *
 * - ui_font_roboto_bold_17 and ui_font_roboto_bold_20 contain ASCII plus
 *   U+00A0..U+017F, so every accent used by HU, DE, ES and FR renders.
 * - ui_font_reboto_bold_24 and ui_font_reboto_reguar_60 are ASCII only.
 *   ui_sec uses the 24px font, so STR_SEC has to stay plain ASCII in every
 *   language, otherwise it shows up as a placeholder box.
 * - Labels that SquareLine left on the default Montserrat font (also ASCII
 *   only) are moved onto Roboto Bold 20 in i18n_apply_ui().
 */

#include "i18n.h"

#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "buzzer.h"
#include "measure.h"
#include "nvs_storage.h"
#include "ui/ui.h"

#define TAG "i18n"

static lang_id_t s_lang = LANG_EN;

static const char *const s_lang_codes[LANG_COUNT] = {
    [LANG_EN] = "EN",
    [LANG_HU] = "HU",
    [LANG_DE] = "DE",
    [LANG_ES] = "ES",
    [LANG_FR] = "FR",
};

static const char *const s_strings[LANG_COUNT][STR_COUNT] = {
    [LANG_EN] = {
        [STR_SEC]                 = "sec",
        [STR_DELETE]              = "Delete",
        [STR_SAVE]                = "Save",
        [STR_BEEP]                = "Beep",
        [STR_LANGUAGE]            = "Language",
        [STR_ON]                  = "On",
        [STR_OFF]                 = "Off",
        [STR_BYE]                 = "Bye",
        [STR_CHARGING]            = "Charging...",
        [STR_BATTERY_EMPTY]       = "Battery empty",
        [STR_ALERT_NO_DEVICE]     = "No device connected",
        [STR_ALERT_MEASURE_ERROR] = "Measurement error",
        [STR_ALERT_MEASURING]     = "Measuring...",
        [STR_ALERT_MEASURE_DONE]  = "Measurement done",
        [STR_ALERT_STANDBY]       = "Standby",
    },
    [LANG_HU] = {
        [STR_SEC]                 = "sec",
        [STR_DELETE]              = "Törlés",
        [STR_SAVE]                = "Mentés",
        [STR_BEEP]                = "Hangjelzés",
        [STR_LANGUAGE]            = "Nyelv",
        [STR_ON]                  = "Be",
        [STR_OFF]                 = "Ki",
        [STR_BYE]                 = "Viszlát",
        [STR_CHARGING]            = "Töltés...",
        [STR_BATTERY_EMPTY]       = "Akku lemerült",
        [STR_ALERT_NO_DEVICE]     = "Nincs csatlakoztatott eszköz",
        [STR_ALERT_MEASURE_ERROR] = "Méréshiba",
        [STR_ALERT_MEASURING]     = "Mérés folyamatban...",
        [STR_ALERT_MEASURE_DONE]  = "Mérés kész",
        [STR_ALERT_STANDBY]       = "Készenlét",
    },
    [LANG_DE] = {
        [STR_SEC]                 = "sec",
        [STR_DELETE]              = "Löschen",
        [STR_SAVE]                = "Sichern",
        [STR_BEEP]                = "Signalton",
        [STR_LANGUAGE]            = "Sprache",
        [STR_ON]                  = "Ein",
        [STR_OFF]                 = "Aus",
        [STR_BYE]                 = "Tschüss",
        [STR_CHARGING]            = "Lädt...",
        [STR_BATTERY_EMPTY]       = "Akku leer",
        [STR_ALERT_NO_DEVICE]     = "Kein Gerät verbunden",
        [STR_ALERT_MEASURE_ERROR] = "Messfehler",
        [STR_ALERT_MEASURING]     = "Messung läuft...",
        [STR_ALERT_MEASURE_DONE]  = "Messung fertig",
        [STR_ALERT_STANDBY]       = "Bereit",
    },
    [LANG_ES] = {
        [STR_SEC]                 = "sec",
        [STR_DELETE]              = "Borrar",
        [STR_SAVE]                = "Guardar",
        [STR_BEEP]                = "Pitido",
        [STR_LANGUAGE]            = "Idioma",
        [STR_ON]                  = "Sí",
        [STR_OFF]                 = "No",
        [STR_BYE]                 = "Adiós",
        [STR_CHARGING]            = "Cargando...",
        [STR_BATTERY_EMPTY]       = "Batería baja",
        [STR_ALERT_NO_DEVICE]     = "Sin dispositivo",
        [STR_ALERT_MEASURE_ERROR] = "Error de medición",
        [STR_ALERT_MEASURING]     = "Midiendo...",
        [STR_ALERT_MEASURE_DONE]  = "Medición completa",
        [STR_ALERT_STANDBY]       = "Listo",
    },
    [LANG_FR] = {
        [STR_SEC]                 = "sec",
        [STR_DELETE]              = "Effacer",
        [STR_SAVE]                = "Sauver",
        [STR_BEEP]                = "Bip",
        [STR_LANGUAGE]            = "Langue",
        [STR_ON]                  = "Oui",
        [STR_OFF]                 = "Non",
        [STR_BYE]                 = "Au revoir",
        [STR_CHARGING]            = "En charge...",
        [STR_BATTERY_EMPTY]       = "Batterie vide",
        [STR_ALERT_NO_DEVICE]     = "Aucun appareil",
        [STR_ALERT_MEASURE_ERROR] = "Erreur de mesure",
        [STR_ALERT_MEASURING]     = "Mesure en cours...",
        [STR_ALERT_MEASURE_DONE]  = "Mesure terminée",
        [STR_ALERT_STANDBY]       = "Prêt",
    },
};

const char *i18n(str_id_t id)
{
    if ((unsigned)id >= STR_COUNT) {
        return "";
    }

    const char *text = s_strings[s_lang][id];
    if (text == NULL) {
        /* Untranslated entry: show English rather than an empty label. */
        text = s_strings[LANG_EN][id];
    }

    return (text != NULL) ? text : "";
}

lang_id_t i18n_get_lang(void)
{
    return s_lang;
}

const char *i18n_lang_code(lang_id_t lang)
{
    if ((unsigned)lang >= LANG_COUNT) {
        lang = LANG_EN;
    }

    return s_lang_codes[lang];
}

lang_id_t i18n_lang_from_code(const char *code)
{
    if (code != NULL) {
        for (int i = 0; i < LANG_COUNT; i++) {
            if (strcasecmp(code, s_lang_codes[i]) == 0) {
                return (lang_id_t)i;
            }
        }
    }

    return LANG_EN;
}

void i18n_select_lang(lang_id_t lang)
{
    if ((unsigned)lang >= LANG_COUNT) {
        lang = LANG_EN;
    }

    s_lang = lang;
}

void i18n_select_lang_by_code(const char *code)
{
    i18n_select_lang(i18n_lang_from_code(code));
}

esp_err_t i18n_set_lang(lang_id_t lang)
{
    i18n_select_lang(lang);

    esp_err_t err = nvs_settings_set_lang(i18n_lang_code(s_lang));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not store language %s: %s",
                 i18n_lang_code(s_lang), esp_err_to_name(err));
    }

    /* Refresh the labels even if the write failed, the choice is still active
     * for this session. */
    i18n_apply_ui();

    return err;
}

lang_id_t i18n_cycle_lang(void)
{
    i18n_set_lang((lang_id_t)((s_lang + 1) % LANG_COUNT));

    return s_lang;
}

/**
 * @brief Set a label's text if the widget exists
 *
 * The screens are destroyed and recreated in places, so every widget pointer
 * has to be checked before use.
 */
static void i18n_set_label(lv_obj_t *label, str_id_t id)
{
    if (label != NULL) {
        lv_label_set_text(label, i18n(id));
    }
}

/**
 * @brief Move a label onto a font that has accented glyphs
 */
static void i18n_set_accent_font(lv_obj_t *label)
{
    if (label != NULL) {
        lv_obj_set_style_text_font(label, &ui_font_roboto_bold_20,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void i18n_apply_ui(void)
{
    /* The LVGL port mutex is recursive, so this is also safe to call from an
     * event callback that already runs inside the LVGL task. */
    lvgl_port_lock(-1);

    /* Dashboard. ui_sec is owned by measure.c: it holds either the translated
     * "sec" or the PMA unit, depending on the selected measurement unit, so it
     * is refreshed through measure_refresh_units() below. */
    i18n_set_label(ui_delLbl, STR_DELETE);
    i18n_set_label(ui_saveLbl, STR_SAVE);

    /* Slide-up settings menu */
    i18n_set_label(ui_beepLbl, STR_BEEP);
    i18n_set_label(ui_LanguageLbl, STR_LANGUAGE);
    i18n_set_label(ui_beepValue, buzzer_is_enabled() ? STR_ON : STR_OFF);

    if (ui_LanguegValue != NULL) {
        lv_label_set_text(ui_LanguegValue, i18n_lang_code(s_lang));
    }

    /* Shutdown splash. The text is rewritten when the screen is actually shown,
     * this keeps it consistent if the language changes in between. */
    i18n_set_label(ui_bye, STR_BYE);

    /* These three inherit the default Montserrat font, which carries no
     * accented glyphs. */
    i18n_set_accent_font(ui_beepValue);
    i18n_set_accent_font(ui_LanguegValue);
    i18n_set_accent_font(ui_bye);

    lvgl_port_unlock();

    /* The unit label next to the big number and the status line are both
     * written only when something changes, so ask the measurement manager to
     * re-emit them in the new language. */
    measure_refresh_units();
    measure_refresh_alert();
}
