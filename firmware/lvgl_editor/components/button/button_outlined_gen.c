/**
 * @file button_outlined_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "button_outlined_gen.h"
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

lv_obj_t * button_outlined_create(lv_obj_t * parent, const char * label)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_base;
    static lv_style_t style_pressed;
    static lv_style_t style_focused;

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&style_base);
        lv_style_set_bg_opa(&style_base, (255 * 100 / 100));
        lv_style_set_bg_color(&style_base, THEME_BG);
        lv_style_set_border_width(&style_base, 3);
        lv_style_set_border_color(&style_base, THEME_STRUCTURE5);
        lv_style_set_text_color(&style_base, THEME_TEXT);
        lv_style_set_border_opa(&style_base, (255 * 100 / 100));

        lv_style_init(&style_pressed);
        lv_style_set_recolor_opa(&style_pressed, OPA_MUTED);
        lv_style_set_recolor(&style_pressed, THEME_STRUCTURE5);

        lv_style_init(&style_focused);
        lv_style_set_border_color(&style_focused, SETTINGS_THEME_COLOR);

        style_inited = true;
    }

    lv_obj_t * button_0 = button_create(parent, label);
    lv_obj_set_name_static(button_0, "button_outlined_#");

    lv_obj_add_style(button_0, &style_base, 0);
    lv_obj_add_style(button_0, &style_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(button_0, &style_focused, LV_STATE_FOCUS_KEY);

    LV_TRACE_OBJ_CREATE("finished");

    return button_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

