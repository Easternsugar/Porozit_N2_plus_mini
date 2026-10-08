/**
 * @file ui_screens.c
 * @brief Thread-safe screen switch helpers (see ui_screens.h).
 */

#include "ui_screens.h"
#include "esp_lvgl_port.h"
#include "i18n.h"
#include "ui/ui.h"

void ui_show_load_screen(void)
{
    lvgl_port_lock(-1);
    if (ui_load != NULL) {
        /* Hide the goodbye label so the boot splash shows only the logo */
        if (ui_bye != NULL) {
            lv_obj_add_flag(ui_bye, LV_OBJ_FLAG_HIDDEN);
        }
        lv_disp_load_scr(ui_load);
    }
    lvgl_port_unlock();
}

void ui_show_dashboard_screen(void)
{
    lvgl_port_lock(-1);
    if (ui_dashboard != NULL) {
        lv_disp_load_scr(ui_dashboard);
    }
    lvgl_port_unlock();
}

/**
 * @brief Set up ui_charge and make it the active screen
 * @param icon Image shown in the middle of the screen
 * @param text Status text below the icon
 */
static void ui_show_charge_screen_with(const lv_img_dsc_t *icon, const char *text)
{
    lvgl_port_lock(-1);
    if (ui_charge != NULL) {
        if (ui_batt6 != NULL) {
            lv_img_set_src(ui_batt6, icon);
        }
        if (ui_battWrng != NULL) {
            lv_label_set_text(ui_battWrng, text);
        }
        lv_disp_load_scr(ui_charge);
    }
    lvgl_port_unlock();
}

void ui_show_charge_screen(void)
{
    ui_show_charge_screen_with(&ui_img_img_charge_png, i18n(STR_CHARGING));
}

void ui_show_battery_empty_screen(void)
{
    ui_show_charge_screen_with(&ui_img_img_battery_empty_png, i18n(STR_BATTERY_EMPTY));
}

void ui_show_bye_screen(const char *message)
{
    lvgl_port_lock(-1);
    if (ui_load != NULL) {
        if (ui_bye != NULL) {
            if (message != NULL) {
                lv_label_set_text(ui_bye, message);
            }
            lv_obj_clear_flag(ui_bye, LV_OBJ_FLAG_HIDDEN);
        }
        lv_disp_load_scr(ui_load);
    }
    lvgl_port_unlock();
}

/* ---------------------------------------------------------------------------
 * Slide-up settings menu
 *
 * The SquareLine handler (ui_event_dashboard) starts scrollMenu_Animation or
 * hideMenu_Animation on every swipe without checking whether the menu is
 * already open or closed. Those animations are relative: because early_apply is
 * false and a get_value_cb is set, lv_anim_start() adds the object's current y
 * to both the start and the end value (see lv_anim.c). So a second swipe in the
 * same direction moves the panel another 255 px, and the next swipe back only
 * returns it to where it already was.
 *
 * This replaces it with a state driven handler that animates to absolute
 * positions, so the panel can only ever sit open or closed. It lives here, not
 * in ui_dashboard.c, so a UI re-export cannot bring the old behaviour back.
 * ------------------------------------------------------------------------- */

/* Designed-in position of the panel, the animations in ui/ui.c move it exactly
 * from its resting y to this. The closed position is read from the widget at
 * start-up, so moving the panel in SquareLine keeps working. */
#define UI_MENU_Y_OPEN          0
#define UI_MENU_ANIM_MS         500
/* Floor for an interrupted swipe, so reversing halfway is not instant */
#define UI_MENU_ANIM_MIN_MS     120

static bool s_menu_open;
static lv_coord_t s_menu_y_closed;

static void ui_menu_anim_y_cb(void *obj, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)obj, (lv_coord_t)value);
}

/**
 * @brief Animate the panel to its open or closed position
 * @note  Must be called with the LVGL lock held
 */
static void ui_menu_apply(bool open)
{
    if (ui_menu == NULL) {
        return;
    }

    /* Drop an in-flight animation, otherwise a fast double swipe could leave
     * the panel halfway and the next absolute move would jump. */
    lv_anim_del(ui_menu, ui_menu_anim_y_cb);

    const lv_coord_t from = lv_obj_get_y_aligned(ui_menu);
    const lv_coord_t to = open ? UI_MENU_Y_OPEN : s_menu_y_closed;

    /* Scale the duration with the remaining distance so reversing a swipe that
     * is still running feels proportional instead of sluggish. */
    const int32_t span = (s_menu_y_closed != UI_MENU_Y_OPEN)
                       ? LV_ABS(s_menu_y_closed - UI_MENU_Y_OPEN) : 1;
    const int32_t distance = LV_ABS(to - from);
    uint32_t time_ms = (uint32_t)(((int32_t)UI_MENU_ANIM_MS * distance) / span);
    if (time_ms < UI_MENU_ANIM_MIN_MS) {
        time_ms = UI_MENU_ANIM_MIN_MS;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, ui_menu);
    lv_anim_set_exec_cb(&anim, ui_menu_anim_y_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_time(&anim, time_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);

    /* ui_overlay is an invisible full screen object that only blocks taps
     * outside the panel, same handling as the generated code. */
    if (ui_overlay != NULL) {
        if (open) {
            lv_obj_clear_flag(ui_overlay, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui_overlay, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_menu_gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) {
        return;
    }

    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);

    /* Swallow the press only when the gesture actually does something. The
     * generated code called lv_indev_wait_release() first, which also killed
     * the click of a tap that happened to drift far enough to look like a
     * gesture. */
    if (dir == LV_DIR_TOP && !s_menu_open) {
        lv_indev_wait_release(indev);
        s_menu_open = true;
        ui_menu_apply(true);
    } else if (dir == LV_DIR_BOTTOM && s_menu_open) {
        lv_indev_wait_release(indev);
        s_menu_open = false;
        ui_menu_apply(false);
    }
}

void ui_menu_init(void)
{
    lvgl_port_lock(-1);

    if (ui_menu != NULL) {
        s_menu_y_closed = lv_obj_get_y_aligned(ui_menu);
    }

    if (ui_dashboard != NULL) {
        lv_obj_remove_event_cb(ui_dashboard, ui_event_dashboard);
        lv_obj_add_event_cb(ui_dashboard, ui_menu_gesture_cb, LV_EVENT_GESTURE, NULL);
    }

    s_menu_open = false;
    if (ui_overlay != NULL) {
        lv_obj_add_flag(ui_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    lvgl_port_unlock();
}

void ui_set_bluetooth_connected(bool connected)
{
    lvgl_port_lock(-1);
    if (ui_bluetooth != NULL) {
        if (connected) {
            lv_obj_clear_flag(ui_bluetooth, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui_bluetooth, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lvgl_port_unlock();
}
