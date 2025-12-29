/**
 * @file utilization_gauge_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "utilization_gauge_gen.h"
#include "pubmote_ui.h"

/*********************
 *      DEFINES
 *********************/

#define THICKNESS 24

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

lv_obj_t * utilization_gauge_create(lv_obj_t * parent, lv_subject_t * bind_duty_cycle, lv_subject_t * bind_vehicle_state)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_main;
    static lv_style_t style_indicator;
    static lv_style_t style_indicator_warn;
    static lv_style_t style_indicator_danger;
    static lv_style_t style_indicator_critical;
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

        lv_style_init(&style_indicator_warn);
        lv_style_set_arc_color(&style_indicator_warn, THEME_WARN);

        lv_style_init(&style_indicator_danger);
        lv_style_set_arc_color(&style_indicator_danger, THEME_DANGER);

        lv_style_init(&style_indicator_critical);
        lv_style_set_arc_color(&style_indicator_critical, THEME_CRITICAL);

        lv_style_init(&style_knob);
        lv_style_set_opa(&style_knob, (255 * 0 / 100));

        style_inited = true;
    }

    lv_obj_t * lv_arc_0 = lv_arc_create(parent);
    lv_obj_set_name_static(lv_arc_0, "utilization_gauge_#");
    lv_arc_set_min_value(lv_arc_0, 0);
    lv_arc_set_start_angle(lv_arc_0, 120);
    lv_arc_set_bg_start_angle(lv_arc_0, 120);
    lv_arc_set_end_angle(lv_arc_0, 60);
    lv_arc_set_bg_end_angle(lv_arc_0, 60);
    lv_obj_set_state(lv_arc_0, LV_STATE_DISABLED, true);
    lv_arc_set_max_value(lv_arc_0, 100);
    lv_arc_set_mode(lv_arc_0, LV_ARC_MODE_REVERSE);
    lv_arc_bind_value(lv_arc_0, bind_duty_cycle);

    lv_obj_add_style(lv_arc_0, &style_main, 0);
    lv_obj_add_style(lv_arc_0, &style_knob, LV_PART_KNOB);
    lv_obj_add_style(lv_arc_0, &style_indicator, LV_PART_INDICATOR);
    lv_obj_bind_style(lv_arc_0, &style_indicator_warn, LV_PART_INDICATOR, bind_vehicle_state, 1);
    lv_obj_bind_style(lv_arc_0, &style_indicator_danger, LV_PART_INDICATOR, bind_vehicle_state, 2);
    lv_obj_bind_style(lv_arc_0, &style_indicator_critical, LV_PART_INDICATOR, bind_vehicle_state, 3);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_arc_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

