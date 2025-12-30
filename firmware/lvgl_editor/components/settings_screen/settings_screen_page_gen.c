/**
 * @file settings_screen_page_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "settings_screen_page_gen.h"
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

lv_obj_t * settings_screen_page_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");

    lv_obj_t * lv_obj_0 = lv_obj_create(parent);
    lv_obj_set_name_static(lv_obj_0, "settings_screen_page_#");
    lv_obj_set_width(lv_obj_0, lv_pct(100));
    lv_obj_set_height(lv_obj_0, lv_pct(100));
    lv_obj_set_flex_flow(lv_obj_0, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(lv_obj_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(lv_obj_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(lv_obj_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(lv_obj_0, 20, 0);
    lv_obj_set_style_bg_opa(lv_obj_0, (255 * 0 / 100), 0);
    lv_obj_set_style_border_width(lv_obj_0, 0, 0);
    lv_obj_set_style_pad_all(lv_obj_0, 0, 0);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

