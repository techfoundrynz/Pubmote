/**
 * @file footer_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "footer_gen.h"
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

lv_obj_t * footer_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");

    lv_obj_t * lv_obj_0 = lv_obj_create(parent);
    lv_obj_set_name_static(lv_obj_0, "footer_#");
    lv_obj_set_align(lv_obj_0, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_height(lv_obj_0, lv_pct(25));
    lv_obj_set_width(lv_obj_0, lv_pct(100));
    lv_obj_set_flex_flow(lv_obj_0, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(lv_obj_0, LV_FLEX_ALIGN_END, 0);
    lv_obj_set_style_flex_track_place(lv_obj_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(lv_obj_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_bottom(lv_obj_0, UNIT_LG, 0);
    lv_obj_set_style_pad_row(lv_obj_0, UNIT_MD, 0);
    lv_obj_set_style_pad_column(lv_obj_0, UNIT_MD, 0);
    lv_obj_set_style_bg_opa(lv_obj_0, (255 * 0 / 100), 0);
    lv_obj_set_style_border_width(lv_obj_0, 0, 0);
    lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

