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

    lv_obj_t * footer = lv_obj_create(parent);
    lv_obj_set_name_static(footer, "footer_#");
    lv_obj_set_name(footer, "footer");
    lv_obj_set_align(footer, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_height(footer, lv_pct(25));
    lv_obj_set_width(footer, lv_pct(100));
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(footer, LV_FLEX_ALIGN_END, 0);
    lv_obj_set_style_flex_track_place(footer, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(footer, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_bottom(footer, UNIT_LG, 0);
    lv_obj_set_style_pad_row(footer, UNIT_MD, 0);
    lv_obj_set_style_pad_column(footer, UNIT_MD, 0);
    lv_obj_set_style_bg_opa(footer, (255 * 0 / 100), 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_flag(footer, LV_OBJ_FLAG_SCROLLABLE, false);

    LV_TRACE_OBJ_CREATE("finished");

    return footer;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

