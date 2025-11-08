#include "esp_log.h"
#include "utilities/screen_utils.h"
#include "utilities/theme_utils.h"
#include <remote/display.h>
#include <remote/settings.h>
#include <ui/ui.h>

static const char *TAG = "PUBREMOTE-SETTINGS_SCREEN";

bool is_settings_screen_active() {
  lv_obj_t *active_screen = lv_scr_act();
  return active_screen == ui_SettingsScreen;
}

// Event handlers
void settings_screen_load_start(lv_event_t *e) {
  ESP_LOGI(TAG, "Settings screen load start");
  if (LVGL_lock(-1)) {
    apply_ui_scale(NULL);

    // Set the scroll snap
    lv_obj_set_scroll_snap_x(ui_SettingsBody, LV_SCROLL_SNAP_CENTER);
    // lv_obj_scroll_to_x(ui_SettingsBody, 0, LV_ANIM_OFF);

    // Brightness
    lv_slider_set_value(ui_BrightnessSlider, device_settings.bl_level, LV_ANIM_OFF);

    // Double press action
    lv_dropdown_set_selected(ui_DoublePressAction, device_settings.double_press_action);

    // Screen rotation
    lv_dropdown_set_selected(ui_Rotation, device_settings.screen_rotation);

    // Auto off time
    lv_dropdown_set_selected(ui_AutoOffTime, device_settings.auto_off_time);

    // Temp units
    lv_dropdown_set_selected(ui_TempUnits, device_settings.temp_units);

    // Distance units
    lv_dropdown_set_selected(ui_DistanceUnits, device_settings.distance_units);

    // Startup sound
    lv_dropdown_set_selected(ui_StartupSound, device_settings.startup_sound);

    // Theme color
    // lv_colorwheel_set_rgb(ui_ThemeColor, lv_color_hex(device_settings.theme_color)); // TODO

    // Dark text
    if (device_settings.dark_text == DARK_TEXT_ENABLED) {
      lv_obj_add_state(ui_DarkText, LV_STATE_CHECKED);
    }

    lv_obj_add_event_cb(ui_SettingsBody, paged_scroll_event_cb, LV_EVENT_SCROLL, ui_SettingsHeader);
    add_page_scroll_indicators(ui_SettingsHeader, ui_SettingsBody);

    create_navigation_group(ui_SettingsFooter);

    LVGL_unlock();
  }
}

void settings_screen_loaded(lv_event_t *e) {
  ESP_LOGI(TAG, "Settings screen loaded");
}

void settings_screen_unload_start(lv_event_t *e) {
  ESP_LOGI(TAG, "Settings screen unload start");
  lv_obj_remove_event_cb(ui_SettingsBody, paged_scroll_event_cb);
}

void brightness_slider_change(lv_event_t *e) {
  int val = lv_slider_get_value(ui_BrightnessSlider);
  device_settings.bl_level = (uint8_t)val;
  display_set_bl_level(device_settings.bl_level);
}

void double_press_action_select_change(lv_event_t *e) {
  int val = lv_dropdown_get_selected(ui_DoublePressAction);
  device_settings.double_press_action = (StatsDoublePressAction)(val & 0xFF);
}

void auto_off_select_change(lv_event_t *e) {
  int val = lv_dropdown_get_selected(ui_AutoOffTime);
  device_settings.auto_off_time = (AutoOffOptions)(val & 0xFF);
}

void temp_units_select_change(lv_event_t *e) {
  int val = lv_dropdown_get_selected(ui_TempUnits);
  device_settings.temp_units = (TempUnits)(val & 0xFF);
}

void distance_units_select_change(lv_event_t *e) {
  int val = lv_dropdown_get_selected(ui_DistanceUnits);
  device_settings.distance_units = (DistanceUnits)(val & 0xFF);
}

void startup_sound_select_change(lv_event_t *e) {
  int val = lv_dropdown_get_selected(ui_StartupSound);
  device_settings.startup_sound = (StartupSoundOptions)(val & 0xFF);
}

void theme_color_picker_change(lv_event_t *e) {
  // lv_color_t val = lv_colorwheel_get_rgb(ui_ThemeColor);
  // device_settings.theme_color = lv_color_to32(val); // TODO
  reload_theme();
}

void dark_text_switch_change(lv_event_t *e) {
  // Toggle dark text setting
  device_settings.dark_text = device_settings.dark_text ? DARK_TEXT_DISABLED : DARK_TEXT_ENABLED;

  // Reload the theme
  reload_theme();
  lv_coord_t scroll_x = lv_obj_get_scroll_x(ui_SettingsBody);

  lv_obj_t *new_scr = lv_obj_create(NULL);

  // This automatically deletes the old screen
  lv_scr_load(new_scr);

  reload_screens();

  // Re-initialize the screen manually
  ui_SettingsScreen_screen_init();
  lv_scr_load(ui_SettingsScreen);
  settings_screen_load_start(NULL);

  // Scroll to the current setting
  lv_obj_scroll_to(ui_SettingsBody, scroll_x, 0, LV_ANIM_OFF); // No animation
}

void screen_rotation_change(lv_event_t *e) {
  int val = lv_dropdown_get_selected(ui_Rotation);
  uint8_t new_val = (val % 4);
  device_settings.screen_rotation = (ScreenRotation)new_val;
  display_set_rotation(device_settings.screen_rotation);
}

void settings_save(lv_event_t *e) {
  // Brightness
  save_device_settings();
}