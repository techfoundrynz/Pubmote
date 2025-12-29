/**
 * @file settings_screen_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "settings_screen_gen.h"
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

lv_obj_t * settings_screen_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    static bool style_inited = false;

    if (!style_inited) {

        style_inited = true;
    }

    lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
    lv_obj_set_name_static(lv_obj_0, "settings_screen_#");
    lv_obj_set_style_bg_color(lv_obj_0, lv_color_hex(0x000000), 0);
    lv_obj_set_width(lv_obj_0, lv_pct(100));
    lv_obj_set_height(lv_obj_0, lv_pct(100));
    lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_flex_flow(lv_obj_0, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(lv_obj_0, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_style_flex_cross_place(lv_obj_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(lv_obj_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_all(lv_obj_0, 20, 0);

    lv_obj_add_event_cb(lv_obj_0, settings_screen_load_start_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    lv_obj_add_event_cb(lv_obj_0, settings_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(lv_obj_0, settings_screen_unload_start_cb, LV_EVENT_SCREEN_UNLOAD_START, NULL);
    lv_obj_add_event_cb(lv_obj_0, settings_screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
    lv_obj_t * header_0 = header_create(lv_obj_0);
    lv_obj_t * h1_0 = h1_create(header_0, "Settings");
    
    lv_obj_t * div_0 = div_create(lv_obj_0);
    lv_obj_set_style_flex_main_place(div_0, LV_FLEX_ALIGN_START, 0);
    lv_obj_set_style_flex_track_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_flag(div_0, LV_OBJ_FLAG_SCROLLABLE, true);
    lv_obj_set_scroll_snap_x(div_0, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(div_0, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(div_0, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(div_0, lv_pct(55));
    lv_obj_set_width(div_0, lv_pct(100));
    lv_obj_set_flag(div_0, LV_OBJ_FLAG_SCROLL_ONE, true);
    lv_obj_t * div_1 = div_create(div_0);
    lv_obj_set_width(div_1, lv_pct(100));
    lv_obj_set_height(div_1, lv_pct(100));
    lv_obj_set_flex_flow(div_1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_1, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_1, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_1, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(div_1, 20, 0);
    lv_obj_t * slider_0 = slider_create(div_1, 0, 100, 50);
    lv_obj_set_style_width(slider_0, lv_pct(80), 0);
    
    lv_obj_t * label_0 = label_create(div_1, "Screen brightness");
    
    lv_obj_t * div_2 = div_create(div_0);
    lv_obj_set_width(div_2, lv_pct(100));
    lv_obj_set_height(div_2, lv_pct(100));
    lv_obj_set_flex_flow(div_2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_2, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(div_2, 20, 0);
    lv_obj_t * lv_dropdown_0 = lv_dropdown_create(div_2);
    lv_dropdown_set_options(lv_dropdown_0, "aaa");
    
    lv_obj_t * label_1 = label_create(div_2, "Double press action");
    
    lv_obj_t * div_3 = div_create(div_0);
    lv_obj_set_width(div_3, lv_pct(100));
    lv_obj_set_height(div_3, lv_pct(100));
    lv_obj_set_flex_flow(div_3, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_3, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_3, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_3, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(div_3, 20, 0);
    lv_obj_t * lv_dropdown_1 = lv_dropdown_create(div_3);
    lv_dropdown_set_options(lv_dropdown_1, "0 Deg");
    
    lv_obj_t * label_2 = label_create(div_3, "Screen rotation");
    
    lv_obj_t * div_4 = div_create(div_0);
    lv_obj_set_width(div_4, lv_pct(100));
    lv_obj_set_height(div_4, lv_pct(100));
    lv_obj_set_flex_flow(div_4, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_4, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_4, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_4, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(div_4, 20, 0);
    lv_obj_t * lv_dropdown_2 = lv_dropdown_create(div_4);
    lv_dropdown_set_options(lv_dropdown_2, "Disabled");
    
    lv_obj_t * label_3 = label_create(div_4, "Auto-off time");
    
    lv_obj_t * div_5 = div_create(div_0);
    lv_obj_set_width(div_5, lv_pct(100));
    lv_obj_set_height(div_5, lv_pct(100));
    lv_obj_set_flex_flow(div_5, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_5, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_5, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_5, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(div_5, 20, 0);
    lv_obj_t * lv_dropdown_3 = lv_dropdown_create(div_5);
    lv_dropdown_set_options(lv_dropdown_3, "Celcius");
    
    lv_obj_t * label_4 = label_create(div_5, "Temp units");
    
    lv_obj_t * div_6 = div_create(div_0);
    lv_obj_set_width(div_6, lv_pct(100));
    lv_obj_set_height(div_6, lv_pct(100));
    lv_obj_set_flex_flow(div_6, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_6, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_6, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_6, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(div_6, 20, 0);
    lv_obj_t * lv_dropdown_4 = lv_dropdown_create(div_6);
    lv_dropdown_set_options(lv_dropdown_4, "Kilometers");
    
    lv_obj_t * label_5 = label_create(div_6, "Distance units");
    
    lv_obj_t * div_7 = div_create(div_0);
    lv_obj_set_width(div_7, lv_pct(100));
    lv_obj_set_height(div_7, lv_pct(100));
    lv_obj_set_flex_flow(div_7, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_7, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_7, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_7, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(div_7, 20, 0);
    lv_obj_t * lv_dropdown_5 = lv_dropdown_create(div_7);
    lv_dropdown_set_options(lv_dropdown_5, "None");
    
    lv_obj_t * label_6 = label_create(div_7, "Startup sound");
    
    lv_obj_t * div_8 = div_create(div_0);
    lv_obj_set_width(div_8, lv_pct(100));
    lv_obj_set_height(div_8, lv_pct(100));
    lv_obj_set_flex_flow(div_8, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_8, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_8, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_8, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(div_8, 20, 0);
    lv_obj_t * label_7 = label_create(div_8, "Theme color");
    
    lv_obj_t * div_9 = div_create(div_0);
    lv_obj_set_width(div_9, lv_pct(100));
    lv_obj_set_height(div_9, lv_pct(100));
    lv_obj_set_flex_flow(div_9, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_9, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_9, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(div_9, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(div_9, 20, 0);
    lv_obj_t * switch_0 = switch_create(div_9, &settings_dark_text);
    
    lv_obj_t * label_8 = label_create(div_9, "Dark text");
    
    lv_obj_t * footer_0 = footer_create(lv_obj_0);
    lv_obj_t * footer_buttons_0 = footer_buttons_create(footer_0);
    lv_obj_t * button_0 = button_create(footer_buttons_0, "Cancel");
    lv_obj_set_width(button_0, 80);
    lv_obj_add_screen_create_event(button_0, LV_EVENT_CLICKED, menu_screen_create, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0);
    
    lv_obj_t * button_primary_0 = button_primary_create(footer_buttons_0, "Save");
    lv_obj_set_width(button_primary_0, 80);
    lv_obj_add_screen_create_event(button_primary_0, LV_EVENT_CLICKED, menu_screen_create, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

