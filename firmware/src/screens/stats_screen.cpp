#include "screens/stats_screen.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "generated/app-window.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/imu.h"
#include "remote/powermanagement.h"
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
  case BATTERY_DISPLAY_VOLTAGE:
    snprintf(battery_str, sizeof(battery_str), "%.1fV", remoteStats.batteryVoltage);
    break;
  case BATTERY_DISPLAY_PERCENT:
  default:
    snprintf(battery_str, sizeof(battery_str), "%d%%", remoteStats.batteryPercentage);
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

  // 5. Update secondary stat
  char left_val[32] = "";
  char left_lbl[16] = "";

  switch (device_settings.secondary_stat_display) {
  case SECONDARY_STAT_DUTY:
    snprintf(left_val, sizeof(left_val), "%d%%", remoteStats.dutyCycle);
    snprintf(left_lbl, sizeof(left_lbl), "DUTY");
    break;
  case SECONDARY_STAT_TEMPS: {
    bool should_convert = device_settings.temp_units == TEMP_UNITS_FAHRENHEIT;
    float converted_mot = should_convert ? convert_c_to_f(remoteStats.motorTemp) : remoteStats.motorTemp;
    float converted_cont = should_convert ? convert_c_to_f(remoteStats.controllerTemp) : remoteStats.controllerTemp;
    const char *temp_unit = should_convert ? "F" : "C";
    snprintf(left_val, sizeof(left_val), "%.0f%s|%.0f%s", converted_mot, temp_unit, converted_cont, temp_unit);
    snprintf(left_lbl, sizeof(left_lbl), "TEMP");
    break;
  }
  case SECONDARY_STAT_DISTANCE: {
    float trip_dist = remoteStats.tripDistance / 1000.0f;
    if (device_settings.distance_units == DISTANCE_UNITS_IMPERIAL) {
      trip_dist = convert_kph_to_mph(trip_dist);
    }
    const char *dist_unit = (device_settings.distance_units == DISTANCE_UNITS_IMPERIAL) ? "mi" : "km";
    snprintf(left_val, sizeof(left_val), "%.1f%s", trip_dist, dist_unit);
    snprintf(left_lbl, sizeof(left_lbl), "TRIP");
    break;
  }
  }

  bool connected = connection_state == CONNECTION_STATE_CONNECTED;
  const char *state_label = get_connection_state_label();

  // Pocket mode active
  bool pocket_mode_active = is_pocket_mode_enabled();

  // Board state message
  bool should_show_board_state =
      connection_state == CONNECTION_STATE_CONNECTED &&
      (remoteStats.state == BOARD_STATE_RUNNING_FLYWHEEL || remoteStats.state == BOARD_STATE_RUNNING_TILTBACK ||
       remoteStats.state == BOARD_STATE_RUNNING_UPSIDEDOWN || remoteStats.state == BOARD_STATE_RUNNING_WHEELSLIP);

  const char *board_state_msg = "";
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
  slint::SharedString slint_left_val = left_val;
  slint::SharedString slint_left_lbl = left_lbl;
  slint::SharedString slint_battery_str = battery_str;
  slint::SharedString slint_board_state_message = board_state_msg;

  // Push to Slint UI state
  slint::invoke_from_event_loop([=]() {
    ui_update_pending.store(false);

    const auto &state = get_slint_window()->global<UiState>();

    // Centrally force HBM off if a confirm dialog is open and HBM was triggered by raise
    if (device_settings.hbm_mode == HBM_MODE_RAISED && display_get_hbm()) {
      if (state.get_show_confirm_dialog()) {
        display_set_hbm(false);
      }
    }

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
    state.set_secondary_stat_left_value(slint_left_val);
    state.set_secondary_stat_left_label(slint_left_lbl);
    state.set_board_battery(slint_battery_str);
    state.set_rssi(remoteStats.signalStrength);
    state.set_pocket_mode_active(pocket_mode_active);
    state.set_lights_on(false);
    state.set_board_state_message(slint_board_state_message);
    state.set_vehicle_type(remoteStats.vehicleType);
  });
}

// Double press navigates to Menu Screen
static bool double_press_handler() {
  if (device_settings.double_press_action == DOUBLE_PRESS_ACTION_OPEN_MENU) {
    slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Menu); });
  }
  return true;
}

extern "C" void handle_imu_gesture(imu_gesture_t gesture) {
  if (gesture == IMU_GESTURE_DOUBLE_TAP) {
    ESP_LOGI(TAG, "Double tap gesture callback triggered. Resetting sleep timer.");
    reset_sleep_timer();
    return;
  }

  if (device_settings.hbm_mode == HBM_MODE_RAISED && display_supports_hbm() && !is_pocket_mode_enabled()) {
    if (gesture == IMU_GESTURE_RAISED) {
      // Do not trigger HBM raise logic if a confirmation dialog/alert is currently open
      if (get_slint_window() && get_slint_window()->global<UiState>().get_show_confirm_dialog()) {
        return;
      }

      if (!display_get_hbm()) {
        ESP_LOGI(TAG, "Raise-to-HBM: viewing position detected. Enabling HBM.");
        display_set_hbm(true);
      }
      reset_sleep_timer();
    }
    else if (gesture == IMU_GESTURE_TABLE_FLAT) {
      if (display_get_hbm()) {
        ESP_LOGI(TAG, "Table detection: flat & motionless. Disabling HBM.");
        display_set_hbm(false);
      }
    }
    else if (gesture == IMU_GESTURE_LOWERED) {
      if (display_get_hbm()) {
        ESP_LOGI(TAG, "Wrist/remote lowered. Disabling HBM.");
        display_set_hbm(false);
      }
    }
  }
}

// Registration functions called from main flow (replaces LVGL loaded events)
extern "C" void setup_stats_properties() {
  ui_update_pending.store(false);
  stats_register_update_cb(stats_update_screen_display);
  register_primary_button_cb(BUTTON_EVENT_DOUBLE_PRESS, double_press_handler);
  imu_register_gesture_callback(handle_imu_gesture);

  if (get_slint_window()) {
    const auto &state = get_slint_window()->global<UiState>();
    state.on_board_battery_clicked([]() {
      // Cycle board battery format
      if (device_settings.battery_display == BATTERY_DISPLAY_PERCENT) {
        device_settings.battery_display = BATTERY_DISPLAY_VOLTAGE;
      }
      else {
        device_settings.battery_display = BATTERY_DISPLAY_PERCENT;
      }
      save_device_settings();
      stats_update_screen_display();
    });

    state.on_secondary_stat_left_clicked([]() {
      // Cycle secondary stat
      device_settings.secondary_stat_display =
          (SecondaryStatDisplayOption)((device_settings.secondary_stat_display + 1) % 3);
      save_device_settings();
      stats_update_screen_display();
    });
  }

  stats_update_screen_display();
}

extern "C" void teardown_stats_properties() {
  stats_unregister_update_cb(stats_update_screen_display);
  unregister_primary_button_cb(BUTTON_EVENT_DOUBLE_PRESS);
  imu_unregister_gesture_callback(handle_imu_gesture);
  if (device_settings.hbm_mode == HBM_MODE_RAISED) {
    display_set_hbm(false);
  }
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
