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

lv_obj_t * body_create(lv_obj_t * parent, int32_t height)
{
    LV_TRACE_OBJ_CREATE("begin");

    lv_obj_t * lv_obj_0 = lv_obj_create(parent);
    lv_obj_set_name_static(lv_obj_0, "body_#");
    lv_obj_set_height(lv_obj_0, lv_pct(50));
    lv_obj_set_flex_flow(lv_obj_0, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_column(lv_obj_0, 20, 0);
    lv_obj_set_style_pad_all(lv_obj_0, 20, 0);

    lv_obj_remove_style_all(lv_obj_0);
    lv_obj_t * title_0 = title_create(lv_obj_0, "abc");
    
    lv_obj_t * title_1 = title_create(lv_obj_0, "abc");

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

