#include "ui/device_preferences.h"
#include "config.h"
#include "remote/display.h"
#include "remote/led.h"
#include "remote/remoteinputs.h"
#include "remote/settings.h"
#include "remote/stats.h"
#include "screens/settings_screen.h"
#include "ui/slint_window.h"
#include <algorithm>
extern "C" const char *ui_hbm_mode_label(HbmModeOptions mode) {
  switch (mode) {
  case HBM_MODE_OFF:
    return "Off";
  case HBM_MODE_ON:
    return "On";
  case HBM_MODE_RAISED:
    return "Raised";
  default:
    return "Off";
  }
}

extern "C" void ui_set_input_support(bool supported) {
  if (!get_slint_window()) {
    // UI not up yet - connect_callbacks() picks the values up on init
    return;
  }

  const bool x_supported = input_pins_x_enabled();
  const bool y_supported = input_pins_y_enabled();
  const bool button_supported = input_pins_button_enabled();

  slint::invoke_from_event_loop([supported, x_supported, y_supported, button_supported]() {
    const auto &state = get_slint_window()->global<UiState>();
    state.set_joystick_supported(supported);
    state.set_joystick_x_supported(x_supported);
    state.set_joystick_y_supported(y_supported);
    state.set_button_supported(button_supported);
  });
}

extern "C" void ui_apply_theme() {
  const auto &theme = get_slint_window()->global<Theme>();

  // Set the actual panel resolution dynamically based on the current screen size
  theme.set_panel_res(std::min(HOR_RES, VER_RES));
  theme.set_panel_width(HOR_RES);
  theme.set_panel_height(VER_RES);

  // Set the screen shape mode (false = circular, true = square/rectangular)
  theme.set_is_square_mode(UI_SHAPE == 1);

  const DeviceSettings preferences = settings_get_device();
  uint8_t r = (preferences.theme_color >> 16) & 0xFF;
  uint8_t g = (preferences.theme_color >> 8) & 0xFF;
  uint8_t b = preferences.theme_color & 0xFF;

  // Custom accent color
  theme.set_accent(slint::Color::from_rgb_uint8(r, g, b));
  float luminance = 0.299f * r + 0.587f * g + 0.114f * b;

  if (luminance > 140.0f) {
    theme.set_primary_button_text(slint::Color::from_rgb_uint8(0, 0, 0));
  }
  else {
    theme.set_primary_button_text(slint::Color::from_rgb_uint8(255, 255, 255));
  }

  theme.set_bg(slint::Color::from_rgb_uint8(0, 0, 0));
  theme.set_text(slint::Color::from_rgb_uint8(255, 255, 255));
  theme.set_text_dim(slint::Color::from_rgb_uint8(154, 154, 154));
}

extern "C" void ui_refresh_device_settings(const DeviceSettings *previous) {
  if (!get_slint_window()) {
    return;
  }
  DeviceSettings old = *previous;
  setup_settings_properties();
  slint::invoke_from_event_loop([old]() {
    if (!get_slint_window()) {
      return;
    }
    const DeviceSettings preferences = settings_get_device();
    display_set_bl_level(preferences.bl_level);
    if (old.screen_rotation != preferences.screen_rotation) {
      display_set_rotation(preferences.screen_rotation);
    }
    if (old.hbm_mode != preferences.hbm_mode) {
      display_set_hbm(preferences.hbm_mode == HBM_MODE_ON);
    }
    ui_apply_theme();
    led_apply_mode();
    const auto &state = get_slint_window()->global<UiState>();
    state.set_pocket_mode_active(is_pocket_mode_enabled());
    state.set_hbm_mode_label(ui_hbm_mode_label(preferences.hbm_mode));
    state.set_led_mode_label(led_mode_label(preferences.led_mode));
    stats_update();
  });
}
