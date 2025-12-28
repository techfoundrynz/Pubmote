/**
 * @file stats_screen_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "stats_screen_gen.h"
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

lv_obj_t * stats_screen_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    static bool style_inited = false;

    if (!style_inited) {

        style_inited = true;
    }

    if (stats_screen == NULL) stats_screen = lv_obj_create(NULL);
    lv_obj_t * lv_obj_0 = stats_screen;
    lv_obj_set_name_static(lv_obj_0, "stats_screen_#");
    lv_obj_set_style_bg_color(lv_obj_0, lv_color_hex(0x000000), 0);

    lv_obj_t * speed_gauge_0 = speed_gauge_create(lv_obj_0);
    lv_arc_set_start_angle(speed_gauge_0, 120);
    lv_arc_set_end_angle(speed_gauge_0, 60);
    lv_obj_set_state(speed_gauge_0, LV_STATE_DISABLED, true);
    lv_arc_set_min_value(speed_gauge_0, 0);
    lv_arc_set_max_value(speed_gauge_0, 100);
    lv_arc_bind_value(speed_gauge_0, &speed);
    
    lv_obj_t * utilization_gauge_0 = utilization_gauge_create(lv_obj_0);
    lv_arc_set_start_angle(utilization_gauge_0, 120);
    lv_arc_set_end_angle(utilization_gauge_0, 60);
    lv_obj_set_state(utilization_gauge_0, LV_STATE_DISABLED, true);
    lv_arc_set_min_value(utilization_gauge_0, 0);
    lv_arc_set_max_value(utilization_gauge_0, 100);
    lv_arc_bind_value(utilization_gauge_0, &duty_cycle);
    lv_obj_set_style_pad_all(utilization_gauge_0, 40, 0);
    
    lv_obj_t * div_0 = div_create(lv_obj_0);
    lv_obj_set_width(div_0, lv_pct(100));
    lv_obj_set_height(div_0, lv_pct(100));
    lv_obj_set_flag(div_0, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_flex_flow(div_0, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(div_0, 78, 0);
    lv_obj_set_style_pad_bottom(div_0, 20, 0);
    lv_obj_set_style_pad_hor(div_0, 48, 0);
    lv_obj_t * div_1 = div_create(div_0);
    lv_obj_set_flex_flow(div_1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_1, LV_FLEX_ALIGN_START, 0);
    lv_obj_set_style_flex_cross_place(div_1, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_1, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_height(div_1, lv_pct(20));
    lv_obj_t * battery_gauge_0 = battery_gauge_create(div_1);
    
    lv_obj_t * div_2 = div_create(div_0);
    lv_obj_set_flex_flow(div_2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_height(div_2, lv_pct(60));
    lv_obj_t * lv_label_0 = lv_label_create(div_2);
    lv_label_bind_text(lv_label_0, &speed, NULL);
    lv_obj_set_style_text_color(lv_label_0, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lv_label_0, inter_bold_72, 0);
    lv_obj_set_flag(lv_label_0, LV_OBJ_FLAG_OVERFLOW_VISIBLE, true);
    lv_obj_t * lv_label_1 = lv_label_create(lv_label_0);
    lv_label_bind_text(lv_label_1, &settings_speed_label, NULL);
    lv_obj_set_style_text_color(lv_label_1, lv_color_hex(0xa9a9a9), 0);
    lv_obj_set_style_text_font(lv_label_1, inter_bold_24, 0);
    lv_obj_set_align(lv_label_1, LV_ALIGN_BOTTOM_RIGHT);
    lv_obj_set_style_translate_x(lv_label_1, 40, 0);
    lv_obj_set_style_translate_y(lv_label_1, -15, 0);
    
    lv_obj_t * lv_label_2 = lv_label_create(div_2);
    lv_label_bind_text(lv_label_2, &duty_cycle, "%d%% Duty Cycle");
    lv_obj_set_style_text_color(lv_label_2, lv_color_hex(0xFFFFFF), 0);
    
    lv_obj_t * div_3 = div_create(div_0);
    lv_obj_set_flex_flow(div_3, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_3, LV_FLEX_ALIGN_END, 0);
    lv_obj_set_style_flex_cross_place(div_3, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_3, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_height(div_3, lv_pct(20));
    lv_obj_t * lv_label_3 = lv_label_create(div_3);
    lv_label_set_text(lv_label_3, "0%");
    lv_obj_set_style_text_color(lv_label_3, lv_color_hex(0xFFFFFF), 0);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

