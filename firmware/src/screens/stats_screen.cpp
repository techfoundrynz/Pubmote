#include "screens/stats_screen.h"
#include "esp_log.h"
#include "remote/display.h"
#include "remote/remoteinputs.h"
#include "remote/vehicle_state.h"
#include <math.h>
#include "remote/connection.h"
#include "remote/settings.h"
#include "remote/stats.h"
#include "utilities/conversion_utils.h"
#include "generated/app-window.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-STATS_SCREEN";
static float max_speed = 40.0f;

extern "C" void setup_menu_properties(); // from menu_screen.cpp
extern "C" void teardown_stats_properties();

static const char* get_connection_state_label() {
  switch (connection_state) {
    case CONNECTION_STATE_CONNECTED:
      return "Connected";
    case CONNECTION_STATE_CONNECTING:
      return "Connecting";
    case CONNECTION_STATE_RECONNECTING:
      return "Reconnecting";
    case CONNECTION_STATE_DISCONNECTED:
    default:
      return "Disconnected";
  }
}

// Stats update callback
extern "C" void stats_update_screen_display() {
  if (!get_slint_window()) return;

  // 1. Calculate speed fraction
  if (remoteStats.speed > max_speed) {
    max_speed = remoteStats.speed;
  }
  float speed_fraction = remoteStats.speed / max_speed;
  float duty_fraction = (float)remoteStats.dutyCycle / 100.0f;

  // 2. Format speed string
  float converted_speed = remoteStats.speed;
  if (device_settings.distance_units == DISTANCE_UNITS_IMPERIAL) {
    converted_speed = convert_kph_to_mph(remoteStats.speed);
  }
  char speed_str[16];
  if (converted_speed >= 10.0f) {
    snprintf(speed_str, sizeof(speed_str), "%.0f", converted_speed);
  } else {
    snprintf(speed_str, sizeof(speed_str), "%.1f", converted_speed);
  }

  // 3. Format board battery string
  char battery_str[32] = {0};
  switch (device_settings.battery_display) {
    case BATTERY_DISPLAY_PERCENT:
      snprintf(battery_str, sizeof(battery_str), "%d%%", remoteStats.batteryPercentage);
      break;
    case BATTERY_DISPLAY_VOLTAGE:
      snprintf(battery_str, sizeof(battery_str), "%.1fV", remoteStats.batteryVoltage);
      break;
    case BATTERY_DISPLAY_ALL:
    default:
      snprintf(battery_str, sizeof(battery_str), "%d%% | %.1fV", remoteStats.batteryPercentage, remoteStats.batteryVoltage);
      break;
  }

  // 4. Decode footpad sensor state
  bool left_pad = false;
  bool right_pad = false;
  switch (remoteStats.switchState) {
    case SWITCH_STATE_LEFT:
      left_pad = true;
      break;
    case SWITCH_STATE_RIGHT:
      right_pad = true;
      break;
    case SWITCH_STATE_BOTH:
      left_pad = true;
      right_pad = true;
      break;
    case SWITCH_STATE_OFF:
    default:
      break;
  }

  // 5. Update connection text
  char connection_text[64];
  if (connection_state == CONNECTION_STATE_CONNECTED) {
    // Show stats on connection state
    bool should_convert = device_settings.temp_units == TEMP_UNITS_FAHRENHEIT;
    float converted_mot = should_convert ? convert_c_to_f(remoteStats.motorTemp) : remoteStats.motorTemp;
    float converted_cont = should_convert ? convert_c_to_f(remoteStats.controllerTemp) : remoteStats.controllerTemp;
    const char* temp_unit = should_convert ? "F" : "C";
    float trip_dist = remoteStats.tripDistance / 1000.0f;
    if (device_settings.distance_units == DISTANCE_UNITS_IMPERIAL) {
      trip_dist = convert_kph_to_mph(trip_dist);
    }
    const char* dist_unit = (device_settings.distance_units == DISTANCE_UNITS_IMPERIAL) ? "mi" : "km";

    snprintf(connection_text, sizeof(connection_text), "Duty: %d%% | M: %.0f°%s C: %.0f°%s | %.1f%s",
             remoteStats.dutyCycle, converted_mot, temp_unit, converted_cont, temp_unit, trip_dist, dist_unit);
  } else {
    snprintf(connection_text, sizeof(connection_text), "%s", get_connection_state_label());
  }

  // Push to Slint UI state
  slint::invoke_from_event_loop([=]() {
    const auto &state = get_slint_window()->global<UiState>();
    state.set_speed(speed_str);
    state.set_speed_unit((device_settings.distance_units == DISTANCE_UNITS_IMPERIAL) ? "MPH" : "KPH");
    state.set_speed_fraction(speed_fraction);
    state.set_duty_fraction(duty_fraction);
    state.set_left_pad(left_pad);
    state.set_right_pad(right_pad);
    state.set_remote_battery(remoteStats.remoteBatteryPercentage);
    state.set_connection_text(connection_text);
    state.set_board_battery(battery_str);
  });
}

// Double press navigates to Menu Screen
static bool double_press_handler() {
  if (device_settings.double_press_action == DOUBLE_PRESS_ACTION_OPEN_MENU) {
    teardown_stats_properties();
    slint::invoke_from_event_loop([]() {
      setup_menu_properties();
      get_slint_window()->global<UiState>().set_screen(Screen::Menu);
    });
  }
  return true;
}

// Registration functions called from main flow (replaces LVGL loaded events)
extern "C" void setup_stats_properties() {
  stats_register_update_cb(stats_update_screen_display);
  register_primary_button_cb(BUTTON_EVENT_DOUBLE_PRESS, double_press_handler);
  stats_update_screen_display();
}

extern "C" void teardown_stats_properties() {
  stats_unregister_update_cb(stats_update_screen_display);
  unregister_primary_button_cb(BUTTON_EVENT_DOUBLE_PRESS);
}


extern "C" void handle_splash_tapped() {
  ESP_LOGI(TAG, "Splash screen tapped");
  setup_stats_properties();
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::Stats);
  });
}

extern "C" void handle_menu_back() {
  ESP_LOGI(TAG, "Menu back pressed");
  setup_stats_properties();
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::Stats);
  });
}
