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

    static lv_style_t style_battery_gauge;

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&style_battery_gauge);
        lv_style_set_width(&style_battery_gauge, lv_pct(15));
        lv_style_set_height(&style_battery_gauge, lv_pct(35));

        style_inited = true;
    }

    if (stats_screen == NULL) stats_screen = lv_obj_create(NULL);
    lv_obj_t * lv_obj_0 = stats_screen;
    lv_obj_set_name_static(lv_obj_0, "stats_screen_#");
    lv_obj_set_style_bg_color(lv_obj_0, THEME_BG, 0);

    lv_obj_add_event_cb(lv_obj_0, stats_screen_load_start_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    lv_obj_add_event_cb(lv_obj_0, stats_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(lv_obj_0, stats_screen_unload_start_cb, LV_EVENT_SCREEN_UNLOAD_START, NULL);
    lv_obj_add_event_cb(lv_obj_0, stats_screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
    lv_obj_add_event_cb(lv_obj_0, stats_screen_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_t * speed_gauge_0 = speed_gauge_create(lv_obj_0, &state_speed, &state_max_speed);
    
    lv_obj_t * utilization_gauge_0 = utilization_gauge_create(lv_obj_0, &state_duty_cycle, &state_vehicle_state);
    lv_obj_set_style_pad_all(utilization_gauge_0, 40, 0);
    
    lv_obj_t * footpad_indicator_0 = footpad_indicator_create(lv_obj_0, &state_adc1_active, &state_adc2_active);
    
    lv_obj_t * div_0 = div_create(lv_obj_0);
    lv_obj_set_width(div_0, lv_pct(100));
    lv_obj_set_height(div_0, lv_pct(100));
    lv_obj_set_flag(div_0, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_flex_flow(div_0, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(div_0, 80, 0);
    lv_obj_set_style_pad_bottom(div_0, 20, 0);
    lv_obj_set_style_pad_hor(div_0, 65, 0);
    lv_obj_t * div_1 = div_create(div_0);
    lv_obj_set_flex_flow(div_1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_1, LV_FLEX_ALIGN_START, 0);
    lv_obj_set_style_flex_cross_place(div_1, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_1, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_height(div_1, lv_pct(20));
    lv_obj_t * battery_gauge_0 = battery_gauge_create(div_1, &state_battery_percent, &state_battery_status);
    lv_obj_add_style(battery_gauge_0, &style_battery_gauge, 0);
    
    lv_obj_t * div_2 = div_create(div_0);
    lv_obj_set_flex_flow(div_2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_height(div_2, lv_pct(60));
    lv_obj_t * dynamic_fmt_label_0 = dynamic_fmt_label_create(div_2);
    dynamic_fmt_label_bind_text(dynamic_fmt_label_0, &state_speed);
    dynamic_fmt_label_bind_text_fmt(dynamic_fmt_label_0, &state_speed_fmt);
    lv_obj_set_style_text_color(dynamic_fmt_label_0, THEME_TEXT, 0);
    lv_obj_set_style_text_font(dynamic_fmt_label_0, inter_bold_96, 0);
    lv_obj_set_flag(dynamic_fmt_label_0, LV_OBJ_FLAG_OVERFLOW_VISIBLE, true);
    lv_obj_set_width(dynamic_fmt_label_0, lv_pct(100));
    lv_obj_set_style_text_align(dynamic_fmt_label_0, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_align(dynamic_fmt_label_0, LV_ALIGN_CENTER);
    lv_obj_set_style_pad_top(dynamic_fmt_label_0, -25, 0);
    lv_obj_t * lv_label_0 = lv_label_create(dynamic_fmt_label_0);
    lv_label_bind_text(lv_label_0, &settings_speed_label, NULL);
    lv_obj_set_style_text_color(lv_label_0, THEME_STRUCTURE4, 0);
    lv_obj_set_style_text_font(lv_label_0, inter_bold_24, 0);
    lv_obj_set_x(lv_label_0, -30);
    lv_obj_set_y(lv_label_0, -20);
    lv_obj_set_align(lv_label_0, LV_ALIGN_BOTTOM_RIGHT);
    
    lv_obj_t * lv_label_1 = lv_label_create(div_2);
    lv_label_bind_text(lv_label_1, &state_connection_state_label, NULL);
    lv_obj_set_style_text_color(lv_label_1, THEME_TEXT, 0);
    lv_obj_set_style_text_font(lv_label_1, inter_24, 0);
    lv_obj_bind_flag_if_not_eq(lv_label_1, &state_connection_state, LV_OBJ_FLAG_HIDDEN, 0);
    
    lv_obj_t * lv_label_2 = lv_label_create(div_2);
    lv_label_bind_text(lv_label_2, &state_connection_state_label, NULL);
    lv_obj_set_style_text_color(lv_label_2, THEME_TEXT, 0);
    lv_obj_set_style_text_font(lv_label_2, inter_24, 0);
    lv_obj_bind_flag_if_not_eq(lv_label_2, &state_connection_state, LV_OBJ_FLAG_HIDDEN, 2);
    
    lv_obj_t * horizontal_pager_0 = horizontal_pager_create(div_2);
    lv_obj_bind_flag_if_not_eq(horizontal_pager_0, &state_connection_state, LV_OBJ_FLAG_HIDDEN, 1);
    lv_obj_t * horizontal_page_0 = horizontal_page_create(horizontal_pager_0);
    lv_obj_set_height(horizontal_page_0, LV_SIZE_CONTENT);
    lv_obj_t * lv_label_3 = lv_label_create(horizontal_page_0);
    lv_label_bind_text(lv_label_3, &state_duty_cycle, "%d%% Duty Cycle");
    lv_obj_set_style_text_color(lv_label_3, THEME_TEXT, 0);
    lv_obj_set_style_text_font(lv_label_3, inter_24, 0);
    
    lv_obj_t * horizontal_page_1 = horizontal_page_create(horizontal_pager_0);
    lv_obj_set_height(horizontal_page_1, LV_SIZE_CONTENT);
    lv_obj_t * lv_label_4 = lv_label_create(horizontal_page_1);
    lv_label_bind_text(lv_label_4, &state_motor_temp, "M: %.1f | C: %.1f");
    lv_obj_set_style_text_color(lv_label_4, THEME_TEXT, 0);
    lv_obj_set_style_text_font(lv_label_4, inter_24, 0);
    
    lv_obj_t * div_3 = div_create(div_0);
    lv_obj_set_flex_flow(div_3, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_3, LV_FLEX_ALIGN_END, 0);
    lv_obj_set_style_flex_cross_place(div_3, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_3, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_height(div_3, lv_pct(20));
    lv_obj_t * label_0 = label_create(div_3, "0%");
    lv_obj_set_style_text_font(label_0, inter_24, 0);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

