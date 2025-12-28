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
    battery_gauge_bind_percent(battery_gauge_0, &state_battery_percent);
    battery_gauge_bind_charge_state(battery_gauge_0, &state_battery_charging);
    
    lv_obj_t * div_2 = div_create(div_0);
    lv_obj_set_flex_flow(div_2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_height(div_2, lv_pct(60));
    lv_obj_t * lv_label_0 = lv_label_create(div_2);
    lv_label_bind_text(lv_label_0, &state_speed, NULL);
    lv_obj_set_style_text_color(lv_label_0, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lv_label_0, inter_bold_72, 0);
    lv_obj_set_flag(lv_label_0, LV_OBJ_FLAG_OVERFLOW_VISIBLE, true);
    
    lv_obj_t * lv_label_1 = lv_label_create(div_2);
    lv_label_bind_text(lv_label_1, &settings_speed_label, NULL);
    lv_obj_set_style_text_color(lv_label_1, lv_color_hex(0xa9a9a9), 0);
    lv_obj_set_style_text_font(lv_label_1, inter_bold_24, 0);
    lv_obj_set_style_x(lv_label_1, 40, 0);
    lv_obj_set_style_y(lv_label_1, 25, 0);
    
    lv_obj_t * lv_label_2 = lv_label_create(div_2);
    lv_label_bind_text(lv_label_2, &state_duty_cycle, "%d%% Duty Cycle");
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

