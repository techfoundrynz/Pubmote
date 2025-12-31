/**
 * @file page_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "page_gen.h"
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

lv_obj_t * page_create(lv_obj_t * parent, const char * text, lv_obj_t * body)
{
    LV_TRACE_OBJ_CREATE("begin");

    lv_obj_t * page = lv_obj_create(parent);
    lv_obj_set_name_static(page, "page_#");
    lv_obj_set_name(page, "page");
    lv_obj_set_width(page, lv_pct(100));
    lv_obj_set_height(page, lv_pct(100));
    lv_obj_set_style_bg_color(page, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flag(page, LV_OBJ_FLAG_SCROLLABLE, false);

    lv_obj_t * header_0 = header_create(page);
    
    lv_obj_t * body_0 = body_create(page);
    lv_obj_t * lv_label_0 = lv_label_create(body_0);
    lv_label_set_text(lv_label_0, "body");
    
    lv_obj_t * footer_0 = footer_create(page);

    LV_TRACE_OBJ_CREATE("finished");

    return page;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

