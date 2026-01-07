/**
 * @file footer_buttons_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "footer_buttons_gen.h"
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

lv_obj_t * footer_buttons_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");

    lv_obj_t * footer_buttons = lv_obj_create(parent);
    lv_obj_set_name_static(footer_buttons, "footer_buttons_#");
    lv_obj_set_name(footer_buttons, "footer_buttons");
    lv_obj_set_flex_flow(footer_buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(footer_buttons, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(footer_buttons, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(footer_buttons, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_bottom(footer_buttons, UNIT_LG, 0);
    lv_obj_set_style_pad_row(footer_buttons, UNIT_MD, 0);
    lv_obj_set_style_pad_column(footer_buttons, UNIT_MD, 0);
    lv_obj_set_style_bg_opa(footer_buttons, (255 * 0 / 100), 0);
    lv_obj_set_style_border_width(footer_buttons, 0, 0);
    lv_obj_set_flag(footer_buttons, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_height(footer_buttons, 50);
    lv_obj_set_width(footer_buttons, lv_pct(100));

    LV_TRACE_OBJ_CREATE("finished");

    return footer_buttons;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

