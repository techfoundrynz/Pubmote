#include "settings_screen.h"
#include "../../pubmote_ui.h"
#include "../utils/object_utils.h"
#ifndef LV_EDITOR_PREVIEW
  #include "remote/settings.h"
#endif

static void settings_screen_set_dropdown_options(lv_obj_t *screen) {
  // Get dropdowns by name
  lv_obj_t *double_press_dropdown = find_child_by_name_recursive(screen, "double_press_action_dropdown");
  lv_obj_t *rotation_dropdown = find_child_by_name_recursive(screen, "screen_rotation_dropdown");
  lv_obj_t *shutdown_dropdown = find_child_by_name_recursive(screen, "shutdown_time_dropdown");
  lv_obj_t *temp_units_dropdown = find_child_by_name_recursive(screen, "temp_units_dropdown");
  lv_obj_t *distance_dropdown = find_child_by_name_recursive(screen, "distance_units_dropdown");
  lv_obj_t *startup_sound_dropdown = find_child_by_name_recursive(screen, "startup_sound_dropdown");

  // Set dropdown options
  if (double_press_dropdown) {
    lv_dropdown_set_options(double_press_dropdown, "None\n"
                                                   "Open menu");
#ifndef LV_EDITOR_PREVIEW
    lv_dropdown_set_selected(double_press_dropdown, device_settings.double_press_action);
#endif
  }

  if (rotation_dropdown) {
    lv_dropdown_set_options(rotation_dropdown, "0 Deg\n"
                                               "90 Deg\n"
                                               "180 Deg\n"
                                               "270 Deg");
#ifndef LV_EDITOR_PREVIEW
    lv_dropdown_set_selected(rotation_dropdown, device_settings.screen_rotation);
#endif
  }

  if (shutdown_dropdown) {
    lv_dropdown_set_options(shutdown_dropdown, "Disabled\n"
                                               "2 minute\n"
                                               "5 minutes\n"
                                               "10 minutes\n"
                                               "20 minutes");

#ifndef LV_EDITOR_PREVIEW
    lv_dropdown_set_selected(shutdown_dropdown, device_settings.auto_off_time);
#endif
  }

  if (temp_units_dropdown) {
    lv_dropdown_set_options(temp_units_dropdown, "Celsius\n"
                                                 "Fahrenheit");

#ifndef LV_EDITOR_PREVIEW
    lv_dropdown_set_selected(temp_units_dropdown, device_settings.temp_units);
#endif
  }

  if (distance_dropdown) {
    lv_dropdown_set_options(distance_dropdown, "Kilometers\n"
                                               "Miles");
#ifndef LV_EDITOR_PREVIEW
    lv_dropdown_set_selected(distance_dropdown, device_settings.distance_units);
#endif
  }

  if (startup_sound_dropdown) {
    lv_dropdown_set_options(startup_sound_dropdown, "None\n"
                                                    "Beep\n"
                                                    "Melody");
#ifndef LV_EDITOR_PREVIEW
    lv_dropdown_set_selected(startup_sound_dropdown, device_settings.startup_sound);
#endif
  }
}

void settings_screen_custom_init(void) {
  // This function exists to force the linker to include this file
}

void settings_screen_load_start_cb(lv_event_t *e) {
  // Handle screen load start events here
  LV_LOG_User("Settings screen load start");
  lv_obj_t *screen = lv_event_get_current_target(e);
  settings_screen_set_dropdown_options(screen);

  // Set other UI elements based on current settings
#ifndef LV_EDITOR_PREVIEW
  lv_obj_t *dark_text_switch = find_child_by_name_recursive(screen, "dark_text_switch");

  if (dark_text_switch) {
    lv_switch_set_state(dark_text_switch, device_settings.dark_text ? true : false);
  }

  // lv_obj_t *theme_color_picker = find_child_by_name_recursive(screen, "theme_color_picker");
#endif
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