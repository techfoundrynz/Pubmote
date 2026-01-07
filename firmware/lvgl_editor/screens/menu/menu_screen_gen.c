/**
 * @file menu_screen_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "menu_screen_gen.h"
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

lv_obj_t * menu_screen_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    static bool style_inited = false;

    if (!style_inited) {

        style_inited = true;
    }

    lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
    lv_obj_set_name_static(lv_obj_0, "menu_screen_#");
    lv_obj_set_style_bg_color(lv_obj_0, THEME_BG, 0);
    lv_obj_set_style_pad_hor(lv_obj_0, 20, 0);
    lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, true);
    lv_obj_set_scrollbar_mode(lv_obj_0, LV_SCROLLBAR_MODE_OFF);

    lv_obj_add_event_cb(lv_obj_0, menu_screen_load_start_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    lv_obj_add_event_cb(lv_obj_0, menu_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(lv_obj_0, menu_screen_unload_start_cb, LV_EVENT_SCREEN_UNLOAD_START, NULL);
    lv_obj_add_event_cb(lv_obj_0, menu_screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
    lv_obj_t * div_0 = div_create(lv_obj_0);
    lv_obj_set_flag(div_0, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_flex_flow(div_0, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(div_0, LV_FLEX_ALIGN_START, 0);
    lv_obj_set_style_flex_cross_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(div_0, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(div_0, UNIT_MD, 0);
    lv_obj_set_style_pad_ver(div_0, 64, 0);
    lv_obj_set_flex_grow(div_0, 1);
    lv_obj_set_scroll_snap_y(div_0, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_flag(div_0, LV_OBJ_FLAG_SCROLL_ONE, true);
    lv_obj_t * button_0 = button_create(div_0, "Back");
    lv_obj_set_height(button_0, 64);
    lv_obj_set_width(button_0, lv_pct(60));
    lv_obj_add_screen_load_event(button_0, LV_EVENT_CLICKED, stats_screen, LV_SCREEN_LOAD_ANIM_MOVE_TOP, 200, 0);
    
    lv_obj_t * button_1 = button_create(div_0, "Enable Pocket Mode");
    lv_obj_set_height(button_1, 64);
    lv_obj_set_width(button_1, lv_pct(60));
    lv_obj_bind_flag_if_eq(button_1, &state_pocket_mode, LV_OBJ_FLAG_HIDDEN, 1);
    lv_obj_add_subject_toggle_event(button_1, &state_pocket_mode, LV_EVENT_CLICKED);
    
    lv_obj_t * button_2 = button_create(div_0, "Disable Pocket Mode");
    lv_obj_set_height(button_2, 64);
    lv_obj_set_width(button_2, lv_pct(60));
    lv_obj_bind_flag_if_eq(button_2, &state_pocket_mode, LV_OBJ_FLAG_HIDDEN, 0);
    lv_obj_add_subject_toggle_event(button_2, &state_pocket_mode, LV_EVENT_CLICKED);
    
    lv_obj_t * button_3 = button_create(div_0, "Settings");
    lv_obj_set_height(button_3, 64);
    lv_obj_set_width(button_3, lv_pct(60));
    lv_obj_add_screen_create_event(button_3, LV_EVENT_CLICKED, settings_screen_create, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0);
    
    lv_obj_t * button_4 = button_create(div_0, "Calibration");
    lv_obj_set_height(button_4, 64);
    lv_obj_set_width(button_4, lv_pct(60));
    lv_obj_add_screen_create_event(button_4, LV_EVENT_CLICKED, calibration_screen_create, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0);
    
    lv_obj_t * button_5 = button_create(div_0, "Pairing");
    lv_obj_set_height(button_5, 64);
    lv_obj_set_width(button_5, lv_pct(60));
    lv_obj_add_screen_create_event(button_5, LV_EVENT_CLICKED, pairing_screen_create, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0);
    
    lv_obj_t * button_6 = button_create(div_0, "About");
    lv_obj_set_height(button_6, 64);
    lv_obj_set_width(button_6, lv_pct(60));
    lv_obj_add_screen_create_event(button_6, LV_EVENT_CLICKED, about_screen_create, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0);
    
    lv_obj_t * button_7 = button_create(div_0, "Shutdown");
    lv_obj_set_height(button_7, 64);
    lv_obj_set_width(button_7, lv_pct(60));
    lv_obj_bind_flag_if_eq(button_7, &state_factory_reset, LV_OBJ_FLAG_HIDDEN, 1);
    lv_obj_add_subject_toggle_event(button_7, &state_factory_reset, LV_EVENT_LONG_PRESSED);
    
    lv_obj_t * button_8 = button_create(div_0, "Factory reset");
    lv_obj_set_height(button_8, 64);
    lv_obj_set_width(button_8, lv_pct(60));
    lv_obj_bind_flag_if_eq(button_8, &state_factory_reset, LV_OBJ_FLAG_HIDDEN, 0);
    lv_obj_add_subject_toggle_event(button_8, &state_factory_reset, LV_EVENT_LONG_PRESSED);
    lv_obj_add_event_cb(button_8, factory_reset, LV_EVENT_CLICKED, NULL);

    LV_TRACE_OBJ_CREATE("finished");

    return lv_obj_0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

