/**
 * @file body_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "body_gen.h"
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

lv_obj_t * body_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");

    lv_obj_t * body = lv_obj_create(parent);
    lv_obj_set_name_static(body, "body_#");
    lv_obj_set_name(body, "body");
    lv_obj_set_height(body, lv_pct(50));
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_column(body, UNIT_MD, 0);
    lv_obj_set_style_pad_all(body, UNIT_MD, 0);
    lv_obj_set_style_flex_main_place(body, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(body, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(body, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(body, (255 * 0 / 100), 0);
    lv_obj_set_style_border_width(body, 0, 0);

    LV_TRACE_OBJ_CREATE("finished");

    return body;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

