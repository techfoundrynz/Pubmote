/**
 * @file header_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "header_gen.h"
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

lv_obj_t * header_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");

    lv_obj_t * header = lv_obj_create(parent);
    lv_obj_set_name_static(header, "header_#");
    lv_obj_set_name(header, "header");
    lv_obj_set_align(header, LV_ALIGN_TOP_MID);
    lv_obj_set_height(header, lv_pct(25));
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(header, LV_FLEX_ALIGN_START, 0);
    lv_obj_set_style_flex_track_place(header, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(header, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(header, UNIT_LG, 0);
    lv_obj_set_style_pad_row(header, UNIT_MD, 0);
    lv_obj_set_style_pad_column(header, UNIT_MD, 0);
    lv_obj_set_style_bg_opa(header, (255 * 0 / 100), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_flag(header, LV_OBJ_FLAG_SCROLLABLE, false);

    LV_TRACE_OBJ_CREATE("finished");

    return header;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

