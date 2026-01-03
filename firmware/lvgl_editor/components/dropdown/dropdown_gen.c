/**
 * @file dropdown_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "dropdown_gen.h"
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

lv_obj_t * dropdown_create(lv_obj_t * parent, const char * options, int32_t selected)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_main;
    static lv_style_t style_pressed;
    static lv_style_t style_list;
    static lv_style_t style_selected;
    static lv_style_t style_scrollbar;
    static lv_style_t style_indicator;

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&style_main);
        lv_style_set_radius(&style_main, UNIT_MD);
        lv_style_set_bg_opa(&style_main, (255 * 100 / 100));
        lv_style_set_bg_color(&style_main, SURFACE_PRIMARY_LIGHT);
        lv_style_set_text_color(&style_main, TEXT_ON_SURFACE_PRIMARY_LIGHT);
        lv_style_set_pad_hor(&style_main, UNIT_SM);
        lv_style_set_pad_ver(&style_main, UNIT_SM);
        lv_style_set_pad_left(&style_main, UNIT_MD);
        lv_style_set_pad_right(&style_main, UNIT_MD);
        lv_style_set_min_height(&style_main, 40);
        lv_style_set_width(&style_main, lv_pct(100));

        lv_style_init(&style_pressed);
        lv_style_set_recolor_opa(&style_pressed, OPA_MUTED);
        lv_style_set_recolor(&style_pressed, THEME_STRUCTURE5);

        lv_style_init(&style_list);
        lv_style_set_bg_color(&style_list, THEME_STRUCTURE2);
        lv_style_set_text_color(&style_list, THEME_TEXT);
        lv_style_set_text_font(&style_list, THEME_DEFAULT_BUTTON_FONT);
        lv_style_set_radius(&style_list, UNIT_SM);
        lv_style_set_border_width(&style_list, 1);
        lv_style_set_border_color(&style_list, THEME_STRUCTURE3);
        lv_style_set_border_opa(&style_list, (255 * 100 / 100));
        lv_style_set_max_height(&style_list, 200);

        lv_style_init(&style_selected);
        lv_style_set_bg_color(&style_selected, SETTINGS_THEME_COLOR);
        lv_style_set_bg_opa(&style_selected, (255 * 30 / 100));

        lv_style_init(&style_scrollbar);
        lv_style_set_bg_color(&style_scrollbar, SETTINGS_THEME_COLOR);
        lv_style_set_bg_opa(&style_scrollbar, (255 * 80 / 100));
        lv_style_set_width(&style_scrollbar, 4);
        lv_style_set_radius(&style_scrollbar, 2);
        lv_style_set_pad_right(&style_scrollbar, 2);

        lv_style_init(&style_indicator);

        style_inited = true;
    }

    lv_obj_t * lv_dropdown_0 = lv_dropdown_create(parent);
    lv_obj_set_name_static(lv_dropdown_0, "dropdown_#");
    lv_dropdown_set_options(lv_dropdown_0, options);
    lv_dropdown_set_selected(lv_dropdown_0, selected);

    lv_obj_remove_style_all(lv_dropdown_0);
    lv_obj_add_style(lv_dropdown_0, &style_main, 0);
    lv_obj_add_style(lv_dropdown_0, &style_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(lv_dropdown_0, &style_indicator, LV_PART_INDICATOR);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_dropdown_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

