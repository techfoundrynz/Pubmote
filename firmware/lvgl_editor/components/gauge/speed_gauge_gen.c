/**
 * @file speed_gauge_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "speed_gauge_gen.h"
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

lv_obj_t * speed_gauge_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_main;
    static lv_style_t style_indicator;
    static lv_style_t style_knob;

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&style_main);
        lv_style_set_arc_width(&style_main, 32);
        lv_style_set_width(&style_main, lv_pct(100));
        lv_style_set_height(&style_main, lv_pct(100));

        lv_style_init(&style_indicator);
        lv_style_set_bg_color(&style_indicator, SETTINGS_THEME_COLOR);
        lv_style_set_arc_width(&style_indicator, 32);

        lv_style_init(&style_knob);
        lv_style_set_opa(&style_knob, (255 * 0 / 100));

        style_inited = true;
    }

    lv_obj_t * lv_arc_0 = lv_arc_create(parent);
    lv_obj_set_name_static(lv_arc_0, "speed_gauge_#");
    lv_obj_set_state(lv_arc_0, LV_STATE_DISABLED, true);

    lv_obj_add_style(lv_arc_0, &style_main, 0);
    lv_obj_add_style(lv_arc_0, &style_knob, LV_PART_KNOB);
    lv_obj_add_style(lv_arc_0, &style_indicator, LV_PART_INDICATOR);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_arc_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

