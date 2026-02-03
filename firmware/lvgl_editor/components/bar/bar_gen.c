/**
 * @file bar_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "bar_gen.h"
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

lv_obj_t * bar_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_main;
    static lv_style_t style_indicator;

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&style_main);
        lv_style_set_bg_color(&style_main, THEME_STRUCTURE4);
        lv_style_set_bg_opa(&style_main, OPA_MUTED);
        lv_style_set_radius(&style_main, 20);
        lv_style_set_height(&style_main, UNIT_SM);

        lv_style_init(&style_indicator);
        lv_style_set_bg_color(&style_indicator, THEME_STRUCTURE4);
        lv_style_set_bg_opa(&style_indicator, (255 * 100 / 100));
        lv_style_set_radius(&style_indicator, 20);

        style_inited = true;
    }

    lv_obj_t * lv_bar_0 = lv_bar_create(parent);
    lv_obj_set_name_static(lv_bar_0, "bar_#");

    lv_obj_remove_style_all(lv_bar_0);
    lv_obj_add_style(lv_bar_0, &style_main, 0);
    lv_obj_add_style(lv_bar_0, &style_indicator, LV_PART_INDICATOR);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_bar_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

