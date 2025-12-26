/**
 * @file stats_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "stats_gen.h"
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

lv_obj_t * stats_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t arc_main;
    static lv_style_t arc_indicator;
    static lv_style_t arc_knob;

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&arc_main);
        lv_style_set_arc_width(&arc_main, 32);
        lv_style_set_width(&arc_main, lv_pct(100));
        lv_style_set_height(&arc_main, lv_pct(100));

        lv_style_init(&arc_indicator);
        lv_style_set_arc_width(&arc_indicator, 32);

        lv_style_init(&arc_knob);
        lv_style_set_bg_opa(&arc_knob, 0);

        style_inited = true;
    }

    if (stats == NULL) stats = lv_obj_create(NULL);
    lv_obj_t * lv_obj_0 = stats;
    lv_obj_set_name_static(lv_obj_0, "stats_#");
    lv_obj_set_style_bg_color(lv_obj_0, lv_color_hex(0x000000), 0);

    lv_obj_t * lv_arc_0 = lv_arc_create(lv_obj_0);
    lv_arc_set_start_angle(lv_arc_0, 120);
    lv_arc_set_end_angle(lv_arc_0, 60);
    lv_obj_set_state(lv_arc_0, LV_STATE_DISABLED, true);
    lv_arc_set_min_value(lv_arc_0, 0);
    lv_arc_set_max_value(lv_arc_0, 100);
    lv_arc_set_value(lv_arc_0, 50);
    lv_obj_add_style(lv_arc_0, &arc_main, LV_PART_MAIN);
    lv_obj_add_style(lv_arc_0, &arc_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(lv_arc_0, &arc_knob, LV_PART_KNOB);
    
    lv_obj_t * lv_arc_1 = lv_arc_create(lv_obj_0);
    lv_arc_set_start_angle(lv_arc_1, 120);
    lv_arc_set_end_angle(lv_arc_1, 60);
    lv_obj_set_state(lv_arc_1, LV_STATE_DISABLED, true);
    lv_arc_set_min_value(lv_arc_1, 0);
    lv_arc_set_max_value(lv_arc_1, 100);
    lv_arc_set_value(lv_arc_1, 50);
    lv_obj_set_style_pad_all(lv_arc_1, 40, 0);
    lv_obj_add_style(lv_arc_1, &arc_main, LV_PART_MAIN);
    lv_obj_add_style(lv_arc_1, &arc_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(lv_arc_1, &arc_knob, LV_PART_KNOB);
    
    lv_obj_t * div_0 = div_create(lv_obj_0);
    lv_obj_set_width(div_0, lv_pct(100));
    lv_obj_set_height(div_0, lv_pct(100));
    lv_obj_set_flag(div_0, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_flex_flow(div_0, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(div_0, 50, 0);
    lv_obj_set_style_pad_bottom(div_0, 50, 0);
    lv_obj_set_style_pad_hor(div_0, 48, 0);
    lv_obj_t * div_1 = div_create(div_0);
    lv_obj_set_flex_flow(div_1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_1, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_1, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_1, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_height(div_1, lv_pct(20));
    lv_obj_t * lv_label_0 = lv_label_create(div_1);
    lv_label_set_text(lv_label_0, "header");
    
    lv_obj_t * div_2 = div_create(div_0);
    lv_obj_set_flex_flow(div_2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_height(div_2, lv_pct(60));
    lv_obj_t * lv_label_1 = lv_label_create(div_2);
    lv_label_set_text(lv_label_1, "45KPH");
    lv_obj_set_style_text_font(lv_label_1, geist_semibold_28, 0);
    
    lv_obj_t * lv_label_2 = lv_label_create(div_2);
    lv_label_set_text(lv_label_2, "25% Duty Cycle");
    
    lv_obj_t * div_3 = div_create(div_0);
    lv_obj_set_flex_flow(div_3, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_3, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_3, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_3, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_height(div_3, lv_pct(20));
    lv_obj_t * lv_label_3 = lv_label_create(div_3);
    lv_label_set_text(lv_label_3, "footer");

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

