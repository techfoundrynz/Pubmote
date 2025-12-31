/**
 * @file button_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "button_gen.h"
#include "pubmote_ui.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/***********************
 *  STATIC VARIABLES
 **********************/

/***********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * button_create(lv_obj_t * parent, const char * label)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_base;
    static lv_style_t style_pressed;
    static lv_style_t style_focused;

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&style_base);
        lv_style_set_radius(&style_base, UNIT_MD);
        lv_style_set_bg_opa(&style_base, (255 * 100 / 100));
        lv_style_set_width(&style_base, lv_pct(100));
        lv_style_set_pad_hor(&style_base, UNIT_SM);
        lv_style_set_pad_ver(&style_base, UNIT_SM);
        lv_style_set_text_font(&style_base, THEME_DEFAULT_BUTTON_FONT);
        lv_style_set_bg_color(&style_base, SURFACE_PRIMARY_LIGHT);
        lv_style_set_text_color(&style_base, TEXT_ON_SURFACE_PRIMARY_LIGHT);
        lv_style_set_min_height(&style_base, 40);

        lv_style_init(&style_pressed);
        lv_style_set_recolor_opa(&style_pressed, OPA_MUTED);
        lv_style_set_recolor(&style_pressed, THEME_STRUCTURE5);

        lv_style_init(&style_focused);

        style_inited = true;
    }

    lv_obj_t * lv_button_0 = lv_button_create(parent);
    lv_obj_set_name_static(lv_button_0, "button_#");

    lv_obj_remove_style_all(lv_button_0);
    lv_obj_add_style(lv_button_0, &style_base, 0);
    lv_obj_add_style(lv_button_0, &style_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(lv_button_0, &style_focused, LV_STATE_FOCUSED);
    lv_obj_t * lv_label_0 = lv_label_create(lv_button_0);
    lv_label_set_text(lv_label_0, label);
    lv_obj_set_align(lv_label_0, LV_ALIGN_CENTER);
    lv_obj_set_width(lv_label_0, lv_pct(100));
    lv_obj_set_style_text_align(lv_label_0, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lv_label_0, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_button_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

