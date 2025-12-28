/**
 * @file slider_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "slider_gen.h"
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

lv_obj_t * slider_create(lv_obj_t * parent, int32_t min_value, int32_t max_value, int32_t value)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_main;
    static lv_style_t style_indicator;
    static lv_style_t style_knob;

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&style_main);
        lv_style_set_bg_color(&style_main, SURFACE_PRIMARY_LIGHT);
        lv_style_set_bg_opa(&style_main, OPA_MUTED);
        lv_style_set_radius(&style_main, 20);
        lv_style_set_height(&style_main, UNIT_SM);
        lv_style_set_width(&style_main, lv_pct(100));

        lv_style_init(&style_indicator);
        lv_style_set_bg_color(&style_indicator, SETTINGS_THEME_COLOR);
        lv_style_set_bg_opa(&style_indicator, (255 * 10 / 100));
        lv_style_set_radius(&style_indicator, 20);

        lv_style_init(&style_knob);
        lv_style_set_bg_color(&style_knob, SETTINGS_THEME_COLOR);
        lv_style_set_bg_opa(&style_knob, (255 * 100 / 100));
        lv_style_set_pad_all(&style_knob, UNIT_SM);
        lv_style_set_radius(&style_knob, 20);

        style_inited = true;
    }

    lv_obj_t * lv_slider_0 = lv_slider_create(parent);
    lv_obj_set_name_static(lv_slider_0, "slider_#");
    lv_slider_set_value(lv_slider_0, value, false);
    lv_slider_set_min_value(lv_slider_0, min_value);
    lv_slider_set_max_value(lv_slider_0, max_value);

    lv_obj_remove_style_all(lv_slider_0);
    lv_obj_add_style(lv_slider_0, &style_main, 0);
    lv_obj_add_style(lv_slider_0, &style_knob, LV_PART_KNOB);
    lv_obj_add_style(lv_slider_0, &style_indicator, LV_PART_INDICATOR);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_slider_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

