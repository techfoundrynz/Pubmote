#include "screens/settings_screen.h"
#include "esp_log.h"
#include "generated/app-window.h"
#include "remote/color_utils.h"
#include "remote/display.h"
#include "remote/led.h"
#include "remote/settings.h"

static const char *TAG = "PUBREMOTE-SETTINGS_SCREEN";

extern "C" void apply_theme_settings(); // from display.cpp

extern "C" void setup_settings_properties() {
  if (!get_slint_window())
    return;

  slint::invoke_from_event_loop([]() {
    const auto &state = get_slint_window()->global<UiState>();

    state.set_brightness((float)device_settings.bl_level);
    state.set_double_press_index(device_settings.double_press_action);
    state.set_rotation_index(device_settings.screen_rotation);
    state.set_auto_off_index(device_settings.auto_off_time);
    state.set_temp_units_index(device_settings.temp_units);
    state.set_distance_units_index(device_settings.distance_units);
    state.set_startup_sound_index(device_settings.startup_sound);

    HSVColor hsv = rgb_to_hsv(device_settings.theme_color);
    state.set_theme_h(hsv.h);
    state.set_theme_s(hsv.s);
    state.set_theme_l(hsv.v);
  });
}

extern "C" void handle_settings_changed() {
  if (!get_slint_window())
    return;

  const auto &state = get_slint_window()->global<UiState>();

  static uint8_t last_bl = 255;
  uint8_t current_bl = (uint8_t)state.get_brightness();

  if (current_bl != last_bl) {
    display_set_bl_level(current_bl);
    last_bl = current_bl;
  }

  static uint32_t last_rgb = 0xFFFFFFFF;
  float h = state.get_theme_h();
  float s = state.get_theme_s();
  float v = state.get_theme_l();
  uint32_t rgb = hsv_to_rgb(h, s, v);

  if (rgb != last_rgb) {
    const auto &theme = get_slint_window()->global<Theme>();

    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    theme.set_accent(slint::Color::from_rgb_uint8(r, g, b));

    float luminance = 0.299f * r + 0.587f * g + 0.114f * b;

    if (luminance > 140.0f) {
      theme.set_primary_button_text(slint::Color::from_rgb_uint8(0, 0, 0));
    }
    else {
      theme.set_primary_button_text(slint::Color::from_rgb_uint8(255, 255, 255));
    }

    // Update physical LED live
    led_set_effect_solid(rgb);

    last_rgb = rgb;
  }
}

extern "C" void handle_settings_save() {
  ESP_LOGI(TAG, "Save settings pressed");
  if (!get_slint_window())
    return;

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

    float h = state.get_theme_h();
    float s = state.get_theme_s();
    float v = state.get_theme_l();
    device_settings.theme_color = hsv_to_rgb(h, s, v);

    save_device_settings();
    display_set_bl_level(device_settings.bl_level);
    display_set_rotation(device_settings.screen_rotation);
    apply_theme_settings();
    led_set_effect_default();

    // Navigate back to Menu Screen
    state.set_screen(Screen::Menu);
  });
}
