#include "screens/settings_screen.h"
#include "esp_log.h"
#include "remote/display.h"
#include "remote/settings.h"
#include "generated/app-window.h"

static const char *TAG = "PUBREMOTE-SETTINGS_SCREEN";

extern "C" void apply_theme_settings(); // from display.cpp

#include <math.h>

static void rgb_to_hsv(uint32_t rgb, float *h, float *s, float *v) {
    float r = ((rgb >> 16) & 0xFF) / 255.0f;
    float g = ((rgb >> 8) & 0xFF) / 255.0f;
    float b = (rgb & 0xFF) / 255.0f;
    
    float cmax = fmax(r, fmax(g, b));
    float cmin = fmin(r, fmin(g, b));
    float diff = cmax - cmin;
    
    if (diff == 0) {
        *h = 0;
    } else if (cmax == r) {
        *h = fmod(60.0f * ((g - b) / diff) + 360.0f, 360.0f);
    } else if (cmax == g) {
        *h = fmod(60.0f * ((b - r) / diff) + 120.0f, 360.0f);
    } else if (cmax == b) {
        *h = fmod(60.0f * ((r - g) / diff) + 240.0f, 360.0f);
    }
    
    if (cmax == 0) {
        *s = 0;
    } else {
        *s = (diff / cmax) * 100.0f;
    }
    
    *v = cmax * 100.0f;
}

static uint32_t hsv_to_rgb(float h, float s, float v) {
    float c = (v / 100.0f) * (s / 100.0f);
    float h_prime = fmod(h / 60.0f, 6.0f);
    float x = c * (1.0f - fabs(fmod(h_prime, 2.0f) - 1.0f));
    float m = (v / 100.0f) - c;
    
    float r = 0, g = 0, b = 0;
    if (h_prime >= 0 && h_prime < 1) { r = c; g = x; b = 0; }
    else if (h_prime >= 1 && h_prime < 2) { r = x; g = c; b = 0; }
    else if (h_prime >= 2 && h_prime < 3) { r = 0; g = c; b = x; }
    else if (h_prime >= 3 && h_prime < 4) { r = 0; g = x; b = c; }
    else if (h_prime >= 4 && h_prime < 5) { r = x; g = 0; b = c; }
    else if (h_prime >= 5 && h_prime < 6) { r = c; g = 0; b = x; }
    
    uint8_t R = (uint8_t)((r + m) * 255.0f);
    uint8_t G = (uint8_t)((g + m) * 255.0f);
    uint8_t B = (uint8_t)((b + m) * 255.0f);
    
    return (R << 16) | (G << 8) | B;
}

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
    state.set_raise_to_hbm_enabled(device_settings.raise_to_hbm);


    float h, s, v;
    rgb_to_hsv(device_settings.theme_color, &h, &s, &v);
    state.set_theme_h(h);
    state.set_theme_s(s);
    state.set_theme_l(v);
  });
}

extern "C" void handle_settings_changed() {
  if (!get_slint_window()) return;

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
      } else {
          theme.set_primary_button_text(slint::Color::from_rgb_uint8(255, 255, 255));
      }
      last_rgb = rgb;
  }
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
    device_settings.raise_to_hbm = state.get_raise_to_hbm_enabled();


    float h = state.get_theme_h();
    float s = state.get_theme_s();
    float v = state.get_theme_l();
    device_settings.theme_color = hsv_to_rgb(h, s, v);

    save_device_settings();
    display_set_bl_level(device_settings.bl_level);
    display_set_rotation(device_settings.screen_rotation);
    apply_theme_settings();

    // Navigate back to Menu Screen
    state.set_screen(Screen::Menu);
  });
}
