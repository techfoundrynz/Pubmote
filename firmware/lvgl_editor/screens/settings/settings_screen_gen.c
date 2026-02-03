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

    static lv_style_t style_dropdown;
    static lv_style_t style_slider;

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&style_dropdown);
        lv_style_set_width(&style_dropdown, lv_pct(50));

        lv_style_init(&style_slider);
        lv_style_set_width(&style_slider, lv_pct(70));

        style_inited = true;
    }

    lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
    lv_obj_set_name_static(lv_obj_0, "settings_screen_#");
    lv_obj_set_style_bg_color(lv_obj_0, THEME_BG, 0);
    lv_obj_set_width(lv_obj_0, lv_pct(100));
    lv_obj_set_height(lv_obj_0, lv_pct(100));
    lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_flex_flow(lv_obj_0, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(lv_obj_0, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_style_flex_cross_place(lv_obj_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(lv_obj_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_all(lv_obj_0, 0, 0);

    lv_obj_add_event_cb(lv_obj_0, settings_screen_load_start_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    lv_obj_add_event_cb(lv_obj_0, settings_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(lv_obj_0, settings_screen_unload_start_cb, LV_EVENT_SCREEN_UNLOAD_START, NULL);
    lv_obj_add_event_cb(lv_obj_0, settings_screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
    lv_obj_t * header_0 = header_create(lv_obj_0);
    lv_obj_t * h1_0 = h1_create(header_0, "Settings");
    
    lv_obj_t * horizontal_pager_0 = horizontal_pager_create(lv_obj_0);
    lv_obj_set_height(horizontal_pager_0, lv_pct(55));
    lv_obj_t * horizontal_page_0 = horizontal_page_create(horizontal_pager_0);
    lv_obj_t * screen_brightness_slider = slider_create(horizontal_page_0, 0, 100, 50);
    lv_obj_set_name(screen_brightness_slider, "screen_brightness_slider");
    lv_obj_add_style(screen_brightness_slider, &style_slider, 0);
    lv_obj_add_event_cb(screen_brightness_slider, settings_screen_brightness_slider_change_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    lv_obj_t * label_0 = label_create(horizontal_page_0, "Screen brightness");
    
    lv_obj_t * horizontal_page_1 = horizontal_page_create(horizontal_pager_0);
    lv_obj_t * double_press_action_dropdown = dropdown_create(horizontal_page_1, "Disabled\nOpen menu", 0);
    lv_obj_set_name(double_press_action_dropdown, "double_press_action_dropdown");
    lv_obj_add_event_cb(double_press_action_dropdown, settings_screen_double_press_action_dropdown_change_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_style(double_press_action_dropdown, &style_dropdown, 0);
    
    lv_obj_t * label_1 = label_create(horizontal_page_1, "Double press action");
    
    lv_obj_t * horizontal_page_2 = horizontal_page_create(horizontal_pager_0);
    lv_obj_t * screen_rotation_dropdown = dropdown_create(horizontal_page_2, "Disabled\n90 deg\n180 deg\n270 deg", 0);
    lv_obj_set_name(screen_rotation_dropdown, "screen_rotation_dropdown");
    lv_obj_add_event_cb(screen_rotation_dropdown, settings_screen_screen_rotation_dropdown_change_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_style(screen_rotation_dropdown, &style_dropdown, 0);
    
    lv_obj_t * label_2 = label_create(horizontal_page_2, "Screen rotation");
    
    lv_obj_t * horizontal_page_3 = horizontal_page_create(horizontal_pager_0);
    lv_obj_t * shutdown_time_dropdown = dropdown_create(horizontal_page_3, "Disabled\n2 minutes\n5 minutes\n10 minutes\n20 minutes", 0);
    lv_obj_set_name(shutdown_time_dropdown, "shutdown_time_dropdown");
    lv_obj_add_event_cb(shutdown_time_dropdown, settings_screen_shutdown_time_dropdown_change_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_style(shutdown_time_dropdown, &style_dropdown, 0);
    
    lv_obj_t * label_3 = label_create(horizontal_page_3, "Auto-off time");
    
    lv_obj_t * horizontal_page_4 = horizontal_page_create(horizontal_pager_0);
    lv_obj_t * temp_units_dropdown = dropdown_create(horizontal_page_4, "Celcius\nFarenheit", 0);
    lv_obj_set_name(temp_units_dropdown, "temp_units_dropdown");
    lv_obj_add_event_cb(temp_units_dropdown, settings_screen_temp_units_dropdown_change_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_style(temp_units_dropdown, &style_dropdown, 0);
    
    lv_obj_t * label_4 = label_create(horizontal_page_4, "Temp units");
    
    lv_obj_t * horizontal_page_5 = horizontal_page_create(horizontal_pager_0);
    lv_obj_t * distance_units_dropdown = dropdown_create(horizontal_page_5, "Kilometers\nMiles", 0);
    lv_obj_set_name(distance_units_dropdown, "distance_units_dropdown");
    lv_obj_add_event_cb(distance_units_dropdown, settings_screen_distance_units_dropdown_change_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_style(distance_units_dropdown, &style_dropdown, 0);
    
    lv_obj_t * label_5 = label_create(horizontal_page_5, "Distance units");
    
    lv_obj_t * horizontal_page_6 = horizontal_page_create(horizontal_pager_0);
    lv_obj_t * startup_sound_dropdown = dropdown_create(horizontal_page_6, "Disabled\nBeep\nMelody", 0);
    lv_obj_set_name(startup_sound_dropdown, "startup_sound_dropdown");
    lv_obj_add_event_cb(startup_sound_dropdown, settings_screen_startup_sound_dropdown_change_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_style(startup_sound_dropdown, &style_dropdown, 0);
    
    lv_obj_t * label_6 = label_create(horizontal_page_6, "Startup sound");
    
    lv_obj_t * horizontal_page_7 = horizontal_page_create(horizontal_pager_0);
    lv_obj_t * color_picker_0 = color_picker_create(horizontal_page_7);
    lv_obj_add_style(color_picker_0, &style_slider, 0);
    
    lv_obj_t * label_7 = label_create(horizontal_page_7, "Theme color");
    
    lv_obj_t * horizontal_page_8 = horizontal_page_create(horizontal_pager_0);
    lv_obj_t * switch_0 = switch_create(horizontal_page_8, &settings_dark_text);
    lv_obj_add_subject_toggle_event(switch_0, &settings_dark_text, LV_EVENT_CLICKED);
    
    lv_obj_t * label_8 = label_create(horizontal_page_8, "Dark text");
    
    lv_obj_t * footer_0 = footer_create(lv_obj_0);
    lv_obj_t * footer_buttons_0 = footer_buttons_create(footer_0);
    lv_obj_t * button_0 = button_create(footer_buttons_0, "Cancel");
    lv_obj_set_width(button_0, 80);
    lv_obj_add_event_cb(button_0, settings_screen_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_screen_create_event(button_0, LV_EVENT_CLICKED, menu_screen_create, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0);
    
    lv_obj_t * button_primary_0 = button_primary_create(footer_buttons_0, "Save");
    lv_obj_set_width(button_primary_0, 80);
    lv_obj_add_event_cb(button_primary_0, settings_screen_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_screen_create_event(button_primary_0, LV_EVENT_CLICKED, menu_screen_create, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

