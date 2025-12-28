/**
 * @file update_screen_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "update_screen_gen.h"
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

lv_obj_t * update_screen_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    static bool style_inited = false;

    if (!style_inited) {

        style_inited = true;
    }

    lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
    lv_obj_set_name_static(lv_obj_0, "update_screen_#");
    lv_obj_set_style_bg_color(lv_obj_0, lv_color_hex(0x000000), 0);
    lv_obj_set_width(lv_obj_0, lv_pct(100));
    lv_obj_set_height(lv_obj_0, lv_pct(100));
    lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_flex_flow(lv_obj_0, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(lv_obj_0, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_style_flex_cross_place(lv_obj_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(lv_obj_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_all(lv_obj_0, 20, 0);

    lv_obj_t * header_0 = header_create(lv_obj_0, "Update");
    
    lv_obj_t * body_0 = body_create(lv_obj_0);
    lv_obj_t * lv_label_0 = lv_label_create(body_0);
    lv_obj_set_style_text_font(lv_label_0, inter_bold_28, 0);
    lv_label_set_text(lv_label_0, "Update");
    lv_obj_set_style_text_color(lv_label_0, lv_color_hex(0xFFFFFF), 0);
    
    lv_obj_t * lv_label_1 = lv_label_create(body_0);
    lv_label_set_text(lv_label_1, "Press next to begin");
    lv_obj_set_style_text_color(lv_label_1, lv_color_hex(0xFFFFFF), 0);
    
    lv_obj_t * footer_0 = footer_create(lv_obj_0, "Footer");
    lv_obj_t * footer_buttons_0 = footer_buttons_create(footer_0);
    lv_obj_t * button_0 = button_create(footer_buttons_0, "Cancel");
    lv_obj_set_width(button_0, 80);
    lv_obj_add_screen_create_event(button_0, LV_EVENT_CLICKED, about_screen_create, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0);
    
    lv_obj_t * button_1 = button_create(footer_buttons_0, "Next");
    lv_obj_set_width(button_1, 80);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

