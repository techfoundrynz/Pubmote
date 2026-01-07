/**
 * @file h1_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "h1_gen.h"
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

lv_obj_t * h1_create(lv_obj_t * parent, const char * text)
{
    LV_TRACE_OBJ_CREATE("begin");

    lv_obj_t * lv_label_0 = lv_label_create(parent);
    lv_obj_set_name_static(lv_label_0, "h1_#");
    lv_obj_set_height(lv_label_0, LV_SIZE_CONTENT);
    lv_obj_set_width(lv_label_0, LV_SIZE_CONTENT);
    lv_label_set_text(lv_label_0, text);
    lv_obj_set_style_text_color(lv_label_0, THEME_TEXT, 0);
    lv_obj_set_style_text_font(lv_label_0, inter_bold_16, 0);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_label_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

