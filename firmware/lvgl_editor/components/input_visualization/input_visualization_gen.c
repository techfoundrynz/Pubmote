/**
 * @file input_visualization_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "input_visualization_gen.h"
#include "pubmote_ui.h"

/*********************
 *      DEFINES
 *********************/

#define THICKNESS 4

#define DEADZONE_THICKNESS 2

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

lv_obj_t * input_visualization_create(lv_obj_t * parent, int32_t x, int32_t y, int32_t deadzone, bool active)
{
    LV_TRACE_OBJ_CREATE("begin");

    lv_obj_t * lv_obj_0 = lv_obj_create(parent);
    lv_obj_set_name_static(lv_obj_0, "input_visualization_#");
    lv_obj_set_height(lv_obj_0, 100);
    lv_obj_set_width(lv_obj_0, 100);
    lv_obj_set_style_pad_all(lv_obj_0, 0, 0);
    lv_obj_set_style_bg_opa(lv_obj_0, (255 * 0 / 100), 0);
    lv_obj_set_style_border_width(lv_obj_0, 0, 0);

    lv_obj_t * outline = lv_obj_create(lv_obj_0);
    lv_obj_set_name(outline, "outline");
    lv_obj_set_width(outline, lv_pct(100));
    lv_obj_set_height(outline, lv_pct(100));
    lv_obj_set_style_radius(outline, lv_pct(50), 0);
    lv_obj_set_style_border_width(outline, THICKNESS, 0);
    lv_obj_set_style_border_color(outline, lv_color_hex(0x4e4e4e), 0);
    lv_obj_set_align(outline, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(outline, (255 * 0 / 100), 0);
    
    lv_obj_t * midline_x = lv_obj_create(lv_obj_0);
    lv_obj_set_name(midline_x, "midline_x");
    lv_obj_set_height(midline_x, THICKNESS);
    lv_obj_set_width(midline_x, lv_pct(100));
    lv_obj_set_style_bg_color(midline_x, lv_color_hex(0xEE4266), 0);
    lv_obj_set_align(midline_x, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(midline_x, (255 * 100 / 100), 0);
    lv_obj_set_style_border_width(midline_x, 0, 0);
    lv_obj_set_style_radius(midline_x, 0, 0);
    
    lv_obj_t * midline_y = lv_obj_create(lv_obj_0);
    lv_obj_set_name(midline_y, "midline_y");
    lv_obj_set_height(midline_y, lv_pct(100));
    lv_obj_set_width(midline_y, THICKNESS);
    lv_obj_set_style_bg_color(midline_y, lv_color_hex(0xEE4266), 0);
    lv_obj_set_align(midline_y, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(midline_y, (255 * 100 / 100), 0);
    lv_obj_set_style_border_width(midline_y, 0, 0);
    lv_obj_set_style_radius(midline_y, 0, 0);
    
    lv_obj_t * deadzone_indicator = lv_obj_create(lv_obj_0);
    lv_obj_set_name(deadzone_indicator, "deadzone_indicator");
    lv_obj_set_width(deadzone_indicator, lv_pct(40));
    lv_obj_set_height(deadzone_indicator, lv_pct(40));
    lv_obj_set_style_border_color(deadzone_indicator, THEME_STRUCTURE5, 0);
    lv_obj_set_style_border_width(deadzone_indicator, DEADZONE_THICKNESS, 0);
    lv_obj_set_style_bg_opa(deadzone_indicator, 0, 0);
    lv_obj_set_style_radius(deadzone_indicator, lv_pct(50), 0);
    lv_obj_set_align(deadzone_indicator, LV_ALIGN_CENTER);
    
    lv_obj_t * indicator = lv_obj_create(lv_obj_0);
    lv_obj_set_name(indicator, "indicator");
    lv_obj_set_height(indicator, 20);
    lv_obj_set_width(indicator, 20);
    lv_obj_set_align(indicator, LV_ALIGN_CENTER);
    lv_obj_set_style_border_width(indicator, 0, 0);
    lv_obj_set_style_bg_opa(indicator, (255 * 0 / 100), 0);
    lv_obj_set_style_pad_all(indicator, 0, 0);
    lv_obj_set_x(indicator, lv_pct(0));
    lv_obj_set_y(indicator, lv_pct(0));
    lv_obj_t * indicator_y = lv_obj_create(indicator);
    lv_obj_set_name(indicator_y, "indicator_y");
    lv_obj_set_height(indicator_y, lv_pct(100));
    lv_obj_set_width(indicator_y, THICKNESS);
    lv_obj_set_style_bg_color(indicator_y, THEME_STRUCTURE5, 0);
    lv_obj_set_align(indicator_y, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(indicator_y, (255 * 100 / 100), 0);
    lv_obj_set_style_border_width(indicator_y, 0, 0);
    
    lv_obj_t * indicator_x = lv_obj_create(indicator);
    lv_obj_set_name(indicator_x, "indicator_x");
    lv_obj_set_height(indicator_x, THICKNESS);
    lv_obj_set_width(indicator_x, lv_pct(100));
    lv_obj_set_style_bg_color(indicator_x, THEME_STRUCTURE5, 0);
    lv_obj_set_align(indicator_x, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(indicator_x, (255 * 100 / 100), 0);
    lv_obj_set_style_border_width(indicator_x, 0, 0);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

