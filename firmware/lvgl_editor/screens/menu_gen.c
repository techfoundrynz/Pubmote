/**
 * @file menu_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "menu_gen.h"
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

lv_obj_t * menu_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    static bool style_inited = false;

    if (!style_inited) {

        style_inited = true;
    }

    lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
    lv_obj_set_name_static(lv_obj_0, "menu_#");

    lv_obj_t * div_0 = div_create(lv_obj_0);
    lv_obj_set_flag(div_0, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_flex_flow(div_0, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_0, LV_FLEX_ALIGN_START, 0);
    lv_obj_set_style_flex_cross_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(div_0, UNIT_MD, 0);
    lv_obj_set_style_pad_hor(div_0, 100, 0);
    lv_obj_set_style_pad_ver(div_0, 64, 0);
    lv_obj_set_flex_grow(div_0, 1);
    lv_obj_set_scroll_snap_y(div_0, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_flag(div_0, LV_OBJ_FLAG_SCROLL_ONE, true);
    lv_obj_t * button_0 = button_create(div_0, "Back");
    lv_obj_set_height(button_0, 64);
    
    lv_obj_t * button_1 = button_create(div_0, "Enable Pocket Mode");
    lv_obj_set_height(button_1, 64);
    
    lv_obj_t * button_2 = button_create(div_0, "Settings");
    lv_obj_set_height(button_2, 64);
    
    lv_obj_t * button_3 = button_create(div_0, "Calibration");
    lv_obj_set_height(button_3, 64);
    
    lv_obj_t * button_4 = button_create(div_0, "Pairing");
    lv_obj_set_height(button_4, 64);
    
    lv_obj_t * button_5 = button_create(div_0, "About");
    lv_obj_set_height(button_5, 64);
    
    lv_obj_t * button_6 = button_create(div_0, "Shutdown");
    lv_obj_set_height(button_6, 64);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

