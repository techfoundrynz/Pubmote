#include "screens/stats_screen.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "generated/app-window.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/receiver.h"
#include "remote/remoteinputs.h"
#include "remote/settings.h"
#include "remote/stats.h"
#include "remote/time.h"
#include "remote/vehicle_state.h"
#include "utilities/conversion_utils.h"
#include <atomic>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-STATS_SCREEN";
static float max_speed = 40.0f;

extern "C" void setup_menu_properties(); // from menu_screen.cpp
extern "C" void teardown_stats_properties();

static const char *get_connection_state_label() {
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

// Adaptive frame dropper: prevents event queue saturation
static std::atomic<bool> ui_update_pending{false};

// Stats update callback
extern "C" void stats_update_screen_display() {
  if (!get_slint_window())
    return;

  // Drop frame if the UI event loop is still processing the last update!
  // This automatically limits the framerate to exactly what the ESP32 can handle,
  // preventing queue growth, memory exhaustion, and touch lag.
  static int dropped_updates = 0;
  if (ui_update_pending.exchange(true)) {
    if (++dropped_updates == 5) {
      ESP_LOGW(TAG, "stats updates dropped: UI event loop not draining");
    }
    return;
  }
  dropped_updates = 0;

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
  }
  else {
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
    snprintf(battery_str, sizeof(battery_str), "%d%% | %.1fV", remoteStats.batteryPercentage,
             remoteStats.batteryVoltage);
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

  // 5. Update stat pages
  char buf1[32] = "";
  char buf2[32] = "";
  char buf3[32] = "";

  if (connection_state == CONNECTION_STATE_CONNECTED) {
    // Show stats on connection state
    bool should_convert = device_settings.temp_units == TEMP_UNITS_FAHRENHEIT;
    float converted_mot = should_convert ? convert_c_to_f(remoteStats.motorTemp) : remoteStats.motorTemp;
    float converted_cont = should_convert ? convert_c_to_f(remoteStats.controllerTemp) : remoteStats.controllerTemp;
    const char *temp_unit = should_convert ? "F" : "C";
    float trip_dist = remoteStats.tripDistance / 1000.0f;
    if (device_settings.distance_units == DISTANCE_UNITS_IMPERIAL) {
      trip_dist = convert_kph_to_mph(trip_dist);
    }
    const char *dist_unit = (device_settings.distance_units == DISTANCE_UNITS_IMPERIAL) ? "mi" : "km";

    snprintf(buf1, sizeof(buf1), "Duty: %d%%", remoteStats.dutyCycle);
    snprintf(buf2, sizeof(buf2), "M: %.0f°%s C: %.0f°%s", converted_mot, temp_unit, converted_cont, temp_unit);
    snprintf(buf3, sizeof(buf3), "Distance: %.1f%s", trip_dist, dist_unit);
  }

  bool connected = connection_state == CONNECTION_STATE_CONNECTED;
  const char *state_label = get_connection_state_label();

  // Calculate RSSI bars
  int rssi_bars = 0;
  if (remoteStats.signalStrength > RSSI_GOOD) {
    rssi_bars = 3;
  }
  else if (remoteStats.signalStrength > RSSI_FAIR) {
    rssi_bars = 2;
  }
  else if (remoteStats.signalStrength > RSSI_POOR) {
    rssi_bars = 1;
  }
  else {
    rssi_bars = 0;
  }

  // Pocket mode active
  bool pocket_mode_active = is_pocket_mode_enabled();

  // Board state message
  bool should_show_board_state =
      connection_state == CONNECTION_STATE_CONNECTED &&
      (remoteStats.state == BOARD_STATE_RUNNING_FLYWHEEL || remoteStats.state == BOARD_STATE_RUNNING_TILTBACK ||
       remoteStats.state == BOARD_STATE_RUNNING_UPSIDEDOWN || remoteStats.state == BOARD_STATE_RUNNING_WHEELSLIP);
       
  const char* board_state_msg = "";
  if (should_show_board_state) {
    switch (remoteStats.state) {
      case BOARD_STATE_RUNNING_FLYWHEEL:
        board_state_msg = "FLYWHEEL";
        break;
      case BOARD_STATE_RUNNING_TILTBACK:
        board_state_msg = "TILTBACK";
        break;
      case BOARD_STATE_RUNNING_WHEELSLIP:
        board_state_msg = "WHEELSLIP";
        break;
      case BOARD_STATE_RUNNING_UPSIDEDOWN:
        board_state_msg = "UPSIDEDOWN";
        break;
      default:
        break;
    }
  }

  // Capture strings safely for the event loop
  slint::SharedString slint_speed_str = speed_str;
  slint::SharedString slint_speed_unit = (device_settings.distance_units == DISTANCE_UNITS_IMPERIAL) ? "MPH" : "KPH";
  slint::SharedString slint_state_label = state_label;
  slint::SharedString slint_buf1 = buf1;
  slint::SharedString slint_buf2 = buf2;
  slint::SharedString slint_buf3 = buf3;
  slint::SharedString slint_battery_str = battery_str;
  slint::SharedString slint_board_state_message = board_state_msg;

  // Push to Slint UI state
  slint::invoke_from_event_loop([=]() {
    ui_update_pending.store(false);

    const auto &state = get_slint_window()->global<UiState>();
    state.set_speed(slint_speed_str);
    state.set_speed_unit(slint_speed_unit);
    state.set_speed_fraction(speed_fraction);
    state.set_duty_fraction(duty_fraction);
    state.set_left_pad(left_pad);
    state.set_right_pad(right_pad);
    state.set_remote_battery(remoteStats.remoteBatteryPercentage);
    state.set_remote_charging(remoteStats.chargeState == CHARGE_STATE_CHARGING ||
                              remoteStats.chargeState == CHARGE_STATE_DONE);
    state.set_is_connected(connected);
    state.set_connection_state_label(slint_state_label);
    state.set_stat_page1(slint_buf1);
    state.set_stat_page2(slint_buf2);
    state.set_stat_page3(slint_buf3);
    state.set_board_battery(slint_battery_str);
    state.set_rssi_bars(rssi_bars);
    state.set_pocket_mode_active(pocket_mode_active);
    state.set_board_state_message(slint_board_state_message);
  });
}

// Double press navigates to Menu Screen
static bool double_press_handler() {
  if (device_settings.double_press_action == DOUBLE_PRESS_ACTION_OPEN_MENU) {
    slint::invoke_from_event_loop([]() {
      get_slint_window()->global<UiState>().set_screen(Screen::Menu);
    });
  }
  return true;
}

// Registration functions called from main flow (replaces LVGL loaded events)
extern "C" void setup_stats_properties() {
  ui_update_pending.store(false);
  stats_register_update_cb(stats_update_screen_display);
  register_primary_button_cb(BUTTON_EVENT_DOUBLE_PRESS, double_press_handler);
  
  if (get_slint_window()) {
    const auto &state = get_slint_window()->global<UiState>();
    state.on_board_battery_long_pressed([]() {
      // Cycle board battery format
      if (device_settings.battery_display == BATTERY_DISPLAY_ALL) {
        device_settings.battery_display = (BoardBatteryDisplayOption)0; // Percent
      } else {
        device_settings.battery_display = (BoardBatteryDisplayOption)(device_settings.battery_display + 1);
      }
      save_device_settings();
      stats_update_screen_display();
    });
  }

  stats_update_screen_display();
}

extern "C" void teardown_stats_properties() {
  stats_unregister_update_cb(stats_update_screen_display);
  unregister_primary_button_cb(BUTTON_EVENT_DOUBLE_PRESS);
}

extern "C" void handle_splash_tapped() {
  ESP_LOGI(TAG, "Splash screen tapped");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Stats); });
}

extern "C" void handle_menu_back() {
  ESP_LOGI(TAG, "Menu back pressed");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Stats); });
}

extern "C" void handle_stats_swiped_down() {
  ESP_LOGI(TAG, "Stats swiped down");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Menu); });
}
