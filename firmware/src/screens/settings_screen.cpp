#include "screens/settings_screen.h"
#include "esp_log.h"
#include "generated/app-window.h"
#include "remote/color_utils.h"
#include "remote/display.h"
#include "remote/led.h"
#include "remote/settings.h"

static const char *TAG = "PUBREMOTE-SETTINGS_SCREEN";

extern "C" void apply_theme_settings(); // from display.cpp

static std::shared_ptr<slint::Model<slint::SharedString>> build_options_model(SettingOptions options) {
  auto model = std::make_shared<slint::VectorModel<slint::SharedString>>();
  for (uint8_t i = 0; i < options.count; i++) {
    model->push_back(options.labels[i]);
  }
  return model;
}

// A dropdown index only maps onto the enum while it is in range. Out of range
// means the UI and the enum have drifted, so keep the stored value rather than
// silently persisting whatever the index happens to land on.
static bool index_in_range(int index, uint8_t count) {
  if (index >= 0 && index < (int)count) {
    return true;
  }
  ESP_LOGE(TAG, "Dropdown index %d out of range (count %u) - keeping stored value", index, count);
  return false;
}

extern "C" void setup_settings_properties() {
  if (!get_slint_window())
    return;

  slint::invoke_from_event_loop([]() {
    const auto &state = get_slint_window()->global<UiState>();

    state.set_double_press_options(build_options_model(settings_double_press_options()));
    state.set_rotation_options(build_options_model(settings_rotation_options()));
    state.set_auto_off_options(build_options_model(settings_auto_off_options()));
    state.set_temp_units_options(build_options_model(settings_temp_units_options()));
    state.set_distance_units_options(build_options_model(settings_distance_units_options()));
    state.set_startup_sound_options(build_options_model(settings_startup_sound_options()));

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

    int index = state.get_double_press_index();
    if (index_in_range(index, settings_double_press_options().count)) {
      device_settings.double_press_action = (StatsDoublePressAction)index;
    }

    index = state.get_rotation_index();
    if (index_in_range(index, settings_rotation_options().count)) {
      device_settings.screen_rotation = (ScreenRotation)index;
    }

    index = state.get_auto_off_index();
    if (index_in_range(index, settings_auto_off_options().count)) {
      device_settings.auto_off_time = (AutoOffOptions)index;
    }

    index = state.get_temp_units_index();
    if (index_in_range(index, settings_temp_units_options().count)) {
      device_settings.temp_units = (TempUnits)index;
    }

    index = state.get_distance_units_index();
    if (index_in_range(index, settings_distance_units_options().count)) {
      device_settings.distance_units = (DistanceUnits)index;
    }

    index = state.get_startup_sound_index();
    if (index_in_range(index, settings_startup_sound_options().count)) {
      device_settings.startup_sound = (StartupSoundOptions)index;
    }

    float h = state.get_theme_h();
    float s = state.get_theme_s();
    float v = state.get_theme_l();
    device_settings.theme_color = hsv_to_rgb(h, s, v);

    save_device_settings();
    display_set_bl_level(device_settings.bl_level);
    display_set_rotation(device_settings.screen_rotation);
    apply_theme_settings();
    led_apply_mode();

    // Navigate back to Menu Screen
    state.set_screen(Screen::Menu);
  });
}
