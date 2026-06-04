#include "screens/settings_screen.h"
#include "esp_log.h"
#include "remote/display.h"
#include "remote/settings.h"
#include "generated/app-window.h"

static const char *TAG = "PUBREMOTE-SETTINGS_SCREEN";

extern "C" void apply_theme_settings(); // from display.cpp

extern "C" void setup_settings_properties() {
  if (!get_slint_window()) return;

  slint::invoke_from_event_loop([]() {
    const auto &state = get_slint_window()->global<UiState>();
    
    state.set_brightness((float)device_settings.bl_level);
    state.set_double_press_index(device_settings.double_press_action);
    state.set_rotation_index(device_settings.screen_rotation);
    state.set_auto_off_index(device_settings.auto_off_time);
    state.set_temp_units_index(device_settings.temp_units);
    state.set_distance_units_index(device_settings.distance_units);
    state.set_startup_sound_index(device_settings.startup_sound);
    state.set_dark_text(device_settings.dark_text);
  });
}

extern "C" void handle_settings_save() {
  ESP_LOGI(TAG, "Save settings pressed");
  if (!get_slint_window()) return;

  // We read the modified settings back from Slint
  slint::invoke_from_event_loop([]() {
    const auto &state = get_slint_window()->global<UiState>();
    
    device_settings.bl_level = (uint8_t)state.get_brightness();
    device_settings.double_press_action = (StatsDoublePressAction)state.get_double_press_index();
    device_settings.screen_rotation = (ScreenRotation)state.get_rotation_index();
    device_settings.auto_off_time = (AutoOffOptions)state.get_auto_off_index();
    device_settings.temp_units = (TempUnits)state.get_temp_units_index();
    device_settings.distance_units = (DistanceUnits)state.get_distance_units_index();
    device_settings.startup_sound = (StartupSoundOptions)state.get_startup_sound_index();
    device_settings.dark_text = state.get_dark_text();

    save_device_settings();
    display_set_bl_level(device_settings.bl_level);
    display_set_rotation(device_settings.screen_rotation);
    apply_theme_settings();

    // Navigate back to Menu Screen
    state.set_screen(Screen::Menu);
  });
}
