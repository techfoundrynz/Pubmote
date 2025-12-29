/**
 * @file footpad_indicator_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "footpad_indicator_gen.h"
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

lv_obj_t * footpad_indicator_create(lv_obj_t * parent, lv_subject_t * bind_adc1, lv_subject_t * bind_adc2)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_indicator;
    static lv_style_t style_knob;

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&style_indicator);
        lv_style_set_opa(&style_indicator, (255 * 0 / 100));
        lv_style_set_arc_rounded(&style_indicator, true);
        lv_style_set_arc_width(&style_indicator, THICKNESS);

        lv_style_init(&style_knob);
        lv_style_set_bg_opa(&style_knob, (255 * 0 / 100));

        style_inited = true;
    }

    lv_obj_t * lv_obj_0 = lv_obj_create(parent);
    lv_obj_set_name_static(lv_obj_0, "footpad_indicator_#");
    lv_obj_set_width(lv_obj_0, lv_pct(100));
    lv_obj_set_height(lv_obj_0, lv_pct(100));

    lv_obj_remove_style_all(lv_obj_0);
    lv_obj_t * lv_arc_0 = lv_arc_create(lv_obj_0);
    lv_arc_set_min_value(lv_arc_0, 0);
    lv_arc_set_max_value(lv_arc_0, 1);
    lv_arc_set_start_angle(lv_arc_0, 93);
    lv_arc_set_end_angle(lv_arc_0, 105);
    lv_arc_set_value(lv_arc_0, 0);
    lv_obj_set_state(lv_arc_0, LV_STATE_DISABLED, true);
    lv_obj_set_style_arc_width(lv_arc_0, THICKNESS, 0);
    lv_obj_add_style(lv_arc_0, &style_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(lv_arc_0, &style_knob, LV_PART_INDICATOR);
    
    lv_obj_t * lv_arc_1 = lv_arc_create(lv_obj_0);
    lv_arc_set_min_value(lv_arc_1, 0);
    lv_arc_set_max_value(lv_arc_1, 1);
    lv_arc_set_start_angle(lv_arc_1, 75);
    lv_arc_set_end_angle(lv_arc_1, 87);
    lv_arc_set_value(lv_arc_1, 0);
    lv_obj_set_state(lv_arc_1, LV_STATE_DISABLED, true);
    lv_obj_set_style_arc_width(lv_arc_1, THICKNESS, 0);
    lv_obj_add_style(lv_arc_1, &style_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(lv_arc_1, &style_knob, LV_PART_INDICATOR);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

