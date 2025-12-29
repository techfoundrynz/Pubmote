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

#define THICKNESS 32

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

lv_obj_t * speed_gauge_create(lv_obj_t * parent, lv_subject_t * bind_speed, lv_subject_t * bind_max_speed)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_main;
    static lv_style_t style_indicator;
    static lv_style_t style_knob;

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&style_main);
        lv_style_set_arc_width(&style_main, THICKNESS);
        lv_style_set_arc_color(&style_main, lv_color_hex(0x878787));
        lv_style_set_width(&style_main, lv_pct(100));
        lv_style_set_height(&style_main, lv_pct(100));
        lv_style_set_arc_rounded(&style_main, true);

        lv_style_init(&style_indicator);
        lv_style_set_arc_color(&style_indicator, SETTINGS_THEME_COLOR);
        lv_style_set_arc_width(&style_indicator, THICKNESS);
        lv_style_set_arc_rounded(&style_indicator, true);

        lv_style_init(&style_knob);
        lv_style_set_opa(&style_knob, (255 * 0 / 100));

        style_inited = true;
    }

    lv_obj_t * dynamic_range_arc_0 = dynamic_range_arc_create(parent);
    lv_obj_set_name_static(dynamic_range_arc_0, "speed_gauge_#");
    lv_arc_set_min_value(dynamic_range_arc_0, 0);
    lv_arc_set_start_angle(dynamic_range_arc_0, 120);
    lv_arc_set_bg_start_angle(dynamic_range_arc_0, 120);
    lv_arc_set_end_angle(dynamic_range_arc_0, 60);
    lv_arc_set_bg_end_angle(dynamic_range_arc_0, 60);
    lv_obj_set_state(dynamic_range_arc_0, LV_STATE_DISABLED, true);
    dynamic_range_arc_bind_value(dynamic_range_arc_0, bind_speed);
    dynamic_range_arc_bind_max_value(dynamic_range_arc_0, bind_max_speed);

    lv_obj_add_style(dynamic_range_arc_0, &style_main, 0);
    lv_obj_add_style(dynamic_range_arc_0, &style_knob, LV_PART_KNOB);
    lv_obj_add_style(dynamic_range_arc_0, &style_indicator, LV_PART_INDICATOR);

    LV_TRACE_OBJ_CREATE("finished");

    return dynamic_range_arc_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

