SET(SOURCES components/ui_comp_menuitem.c
    components/ui_comp.c
    screens/ui_load.c
    screens/ui_dashboard.c
    screens/ui_charge.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_img_hello_logo_png.c
    images/ui_img_img_timer_red_png.c
    images/ui_img_img_trash_icon_png.c
    images/ui_img_img_save_icon_png.c
    images/ui_img_332468020.c
    images/ui_img_img_smartphone_png.c
    images/ui_img_img_battery_full_png.c
    images/ui_img_img_arrow_icon_png.c
    images/ui_img_img_battery_75_png.c
    images/ui_img_img_battery_50_png.c
    images/ui_img_img_battery_empty_png.c
    images/ui_img_img_charge_png.c
    fonts/ui_font_reboto_bold_24.c
    fonts/ui_font_reboto_reguar_60.c
    fonts/ui_font_roboto_bold_17.c
    fonts/ui_font_roboto_bold_20.c)

add_library(ui ${SOURCES})
