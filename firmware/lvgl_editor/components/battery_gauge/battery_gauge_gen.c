/**
 * @file battery_gauge_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "battery_gauge_gen.h"
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

lv_obj_t * battery_gauge_create(lv_obj_t * parent, lv_subject_t * bind_percent, lv_subject_t * bind_charge_state)
{
    LV_TRACE_OBJ_CREATE("begin");

    lv_obj_t * lv_obj_0 = lv_obj_create(parent);
    lv_obj_set_name_static(lv_obj_0, "battery_gauge_#");

    lv_obj_remove_style_all(lv_obj_0);
    lv_obj_t * lv_obj_1 = lv_obj_create(lv_obj_0);
    lv_obj_set_width(lv_obj_1, lv_pct(95));
    lv_obj_set_height(lv_obj_1, lv_pct(100));
    lv_obj_set_style_border_width(lv_obj_1, lv_pct(4), 0);
    lv_obj_set_style_border_color(lv_obj_1, THEME_STRUCTURE5, 0);
    lv_obj_set_style_bg_opa(lv_obj_1, (255 * 0 / 100), 0);
    lv_obj_set_style_radius(lv_obj_1, 5, 0);
    lv_obj_set_style_pad_all(lv_obj_1, 0, 0);
    lv_obj_t * lv_obj_2 = lv_obj_create(lv_obj_1);
    lv_obj_set_style_radius(lv_obj_2, 0, 0);
    lv_obj_set_style_bg_color(lv_obj_2, THEME_SUCCESS, 0);
    lv_obj_set_height(lv_obj_2, lv_pct(100));
    lv_obj_set_width(lv_obj_2, lv_pct(100));
    lv_obj_set_style_bg_opa(lv_obj_2, (255 * 100 / 100), 0);
    lv_obj_set_style_border_width(lv_obj_2, 0, 0);
    lv_obj_set_align(lv_obj_2, LV_ALIGN_LEFT_MID);
    
    lv_obj_t * lv_obj_3 = lv_obj_create(lv_obj_0);
    lv_obj_set_width(lv_obj_3, lv_pct(5));
    lv_obj_set_height(lv_obj_3, lv_pct(30));
    lv_obj_set_style_bg_color(lv_obj_3, THEME_STRUCTURE5, 0);
    lv_obj_set_align(lv_obj_3, LV_ALIGN_RIGHT_MID);
    lv_obj_set_style_border_width(lv_obj_3, 0, 0);
    lv_obj_set_style_radius(lv_obj_3, 0, 0);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

