#include "settings_screen.h"
#include "../../pubmote_ui.h"
#include "../utils/object_utils.h"

static void settings_screen_set_dropdown_options(lv_obj_t * screen) {
  // Get dropdowns by name
    lv_obj_t * double_press_dropdown = find_child_by_name_recursive(screen, "double_press_action_dropdown");
    lv_obj_t * rotation_dropdown = find_child_by_name_recursive(screen, "screen_rotation_dropdown");
    lv_obj_t * shutdown_dropdown = find_child_by_name_recursive(screen, "shutdown_time_dropdown");
    lv_obj_t * temp_units_dropdown = find_child_by_name_recursive(screen, "temp_units_dropdown");
    lv_obj_t * distance_dropdown = find_child_by_name_recursive(screen, "distance_units_dropdown");
    lv_obj_t * startup_sound_dropdown = find_child_by_name_recursive(screen, "startup_sound_dropdown");
    
    // Set dropdown options
    if(double_press_dropdown) {
        lv_dropdown_set_options(double_press_dropdown, 
            "None\n"
            "Open menu");
    }
    
    if(rotation_dropdown) {
        lv_dropdown_set_options(rotation_dropdown, 
            "0 Deg\n"
            "90 Deg\n"
            "180 Deg\n"
            "270 Deg");
    }
    
    if(shutdown_dropdown) {
        lv_dropdown_set_options(shutdown_dropdown, 
            "Disabled\n"
            "2 minute\n"
            "5 minutes\n"
            "10 minutes\n"
            "20 minutes");
    }
    
    if(temp_units_dropdown) {
        lv_dropdown_set_options(temp_units_dropdown, 
            "Celsius\n"
            "Fahrenheit");
    }
    
    if(distance_dropdown) {
        lv_dropdown_set_options(distance_dropdown, 
            "Kilometers\n"
            "Miles");
    }
    
    if(startup_sound_dropdown) {
        lv_dropdown_set_options(startup_sound_dropdown, 
            "None\n"
            "Beep\n"
            "Melody");
    }
}

void settings_screen_custom_init(void) {
  // This function exists to force the linker to include this file
}

void settings_screen_load_start_cb(lv_event_t *e) {
  // Handle screen load start events here
    // LV_LOG_ERROR("STATS");
    lv_obj_t * screen = lv_event_get_current_target(e);
    settings_screen_set_dropdown_options(screen);
}

void settings_screen_loaded_cb(lv_event_t *e) {
  // Handle screen loaded events here
}

void settings_screen_unload_start_cb(lv_event_t *e) {
  // Handle screen unload start events here
}

void settings_screen_unloaded_cb(lv_event_t *e) {
  // Handle screen unloaded events here
}