/**
 * @file charge_screen_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "charge_screen_gen.h"
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

lv_obj_t * charge_screen_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t charge_arc_indicator;
    static lv_style_t charge_arc_knob;

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&charge_arc_indicator);
        lv_style_set_arc_width(&charge_arc_indicator, 24);
        lv_style_set_arc_rounded(&charge_arc_indicator, false);

        lv_style_init(&charge_arc_knob);
        lv_style_set_bg_opa(&charge_arc_knob, 0);

        style_inited = true;
    }

    lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
    lv_obj_set_name_static(lv_obj_0, "charge_screen_#");
    lv_obj_set_style_bg_color(lv_obj_0, lv_color_hex(0x000000), 0);

    lv_obj_add_event_cb(lv_obj_0, charge_screen_load_start_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    lv_obj_add_event_cb(lv_obj_0, charge_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(lv_obj_0, charge_screen_unload_start_cb, LV_EVENT_SCREEN_UNLOAD_START, NULL);
    lv_obj_add_event_cb(lv_obj_0, charge_screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
    lv_obj_t * lv_arc_0 = lv_arc_create(lv_obj_0);
    lv_obj_set_style_arc_width(lv_arc_0, 24, 0);
    lv_obj_set_width(lv_arc_0, lv_pct(100));
    lv_obj_set_height(lv_arc_0, lv_pct(100));
    lv_arc_set_min_value(lv_arc_0, 0);
    lv_arc_set_max_value(lv_arc_0, 100);
    lv_arc_bind_value(lv_arc_0, &state_battery_percent);
    lv_arc_set_start_angle(lv_arc_0, 90);
    lv_arc_set_end_angle(lv_arc_0, 89);
    lv_arc_set_bg_start_angle(lv_arc_0, 90);
    lv_arc_set_bg_end_angle(lv_arc_0, 89);
    lv_obj_set_state(lv_arc_0, LV_STATE_DISABLED, true);
    lv_obj_add_style(lv_arc_0, &charge_arc_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(lv_arc_0, &charge_arc_knob, LV_PART_KNOB);
    
    lv_obj_t * div_0 = div_create(lv_obj_0);
    lv_obj_set_width(div_0, lv_pct(100));
    lv_obj_set_height(div_0, lv_pct(100));
    lv_obj_set_flag(div_0, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_flex_flow(div_0, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_t * lv_label_0 = lv_label_create(div_0);
    lv_label_set_text(lv_label_0, "Charging");
    lv_obj_set_style_text_color(lv_label_0, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lv_label_0, inter_bold_24, 0);
    
    lv_obj_t * lv_label_1 = lv_label_create(div_0);
    lv_label_bind_text(lv_label_1, &state_battery_percent, "%d%%");
    lv_obj_set_style_text_color(lv_label_1, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lv_label_1, inter_24, 0);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

