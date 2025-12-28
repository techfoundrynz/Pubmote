/**
 * @file switch_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "switch_gen.h"
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

lv_obj_t * switch_create(lv_obj_t * parent, lv_subject_t * switched)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t switch_knob;
    static lv_style_t switch_main;
    static lv_style_t switch_indicator;

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&switch_knob);
        lv_style_set_pad_all(&switch_knob, -4);
        lv_style_set_shadow_opa(&switch_knob, 0);
        lv_style_set_bg_color(&switch_knob, BG_PRIMARY_LIGHT);

        lv_style_init(&switch_main);
        lv_style_set_bg_color(&switch_main, SETTINGS_THEME_COLOR);
        lv_style_set_bg_opa(&switch_main, 30);

        lv_style_init(&switch_indicator);
        lv_style_set_bg_color(&switch_indicator, SETTINGS_THEME_COLOR);

        style_inited = true;
    }

    lv_obj_t * lv_switch_0 = lv_switch_create(parent);
    lv_obj_set_name_static(lv_switch_0, "switch_#");
    lv_obj_set_width(lv_switch_0, 50);
    lv_obj_set_height(lv_switch_0, 30);
    lv_obj_bind_checked(lv_switch_0, switched);

    lv_obj_add_style(lv_switch_0, &switch_knob, LV_PART_KNOB);
    lv_obj_add_style(lv_switch_0, &switch_main, LV_PART_MAIN);
    lv_obj_add_style(lv_switch_0, &switch_indicator, LV_PART_INDICATOR | LV_STATE_CHECKED);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_switch_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

