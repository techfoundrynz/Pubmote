#include "screens/stats_screen.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "imu/brightness_switch.h"
#include "imu/pose_brightness.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/imu.h"
#include "remote/input_router.h"
#include "remote/powermanagement.h"
#include "remote/receiver.h"
#include "remote/remoteinputs.h"
#include "remote/settings.h"
#include "remote/settings_state.h"
#include "remote/stats.h"
#include "remote/time.h"
#include "remote/vehicle_state.h"
#include "screens/charge_screen.h"
#include "slint_generated/app-window.h"
#include "ui/screen_status.h"
#include "ui/slint_window.h"
#include "utilities/conversion_utils.h"
#include "utilities/ui_operation.h"
#include <atomic>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-STATS_SCREEN";
static brightness_switch_t pose_brightness_switch;
static bool stats_raised = false; // UI task only.
static std::atomic<uint32_t> stats_session{0};
static void update_stats_brightness(bool immediate);
static float max_speed = 40.0f;

// The dials are full-screen-sized items and Slint tracks dirty regions per item
// bounding box, so any change to an arc's value repaints most of the panel. At 466px
// across with a 260deg sweep, one degree of arc is only ~4px of travel, so telemetry
// jitter far below that buys a full repaint while being invisible.
//
// Quantise to 1/256 - just over a degree - and only push when the arc would actually
// move. Steady riding then costs zero arc invalidation instead of one repaint per
// telemetry tick. The quantised value is what gets pushed, so the arc also lands on
// stable positions rather than drifting sub-pixel.
#define ARC_QUANT_STEPS 256.0f

// Returns true (updating *last) only when the quantised fraction actually moved.
static bool arc_fraction_moved(float value, float *last) {
  float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
  float quantised = roundf(clamped * ARC_QUANT_STEPS) / ARC_QUANT_STEPS;
  if (quantised == *last) {
    return false;
  }
  *last = quantised;
  return true;
}

// printf keeps the sign of anything that rounds to zero, so a reading a hair below it - which is
// where the board parks speed, temps and trip at rest - prints as "-0.0". threshold is half the
// last displayed digit, i.e. the point below which the sign is the only thing left.
static float snap_zero(float value, float threshold) {
  return fabsf(value) < threshold ? 0.0f : value;
}

extern "C" void setup_menu_properties(); // from menu_screen.cpp
extern "C" void teardown_stats_properties();

static const char *get_connection_state_label() {
  switch (connection_get_state()) {
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

  const RemoteStats telemetry = stats_snapshot();
  const DeviceSettings preferences = settings_get_device();

  // 1. Calculate speed fraction
  if (telemetry.speed > max_speed) {
    max_speed = telemetry.speed;
  }
  float speed_fraction = telemetry.speed / max_speed;
  float utilization_fraction = (float)stats_utilization(&telemetry) / 100.0f;

  // 2. Format speed string
  float converted_speed = telemetry.speed;
  if (preferences.distance_units == DISTANCE_UNITS_IMPERIAL) {
    converted_speed = convert_kph_to_mph(telemetry.speed);
  }
  converted_speed = snap_zero(converted_speed, 0.05f);
  char speed_str[16];
  if (roundf(converted_speed * 10.0f) / 10.0f >= 10.0f) {
    snprintf(speed_str, sizeof(speed_str), "%.0f", converted_speed);
  }
  else {
    snprintf(speed_str, sizeof(speed_str), "%.1f", converted_speed);
  }

  // 3. Format board battery string
  char battery_str[32] = {0};
  switch (preferences.battery_display) {
  case BATTERY_DISPLAY_VOLTAGE:
    snprintf(battery_str, sizeof(battery_str), "%.1fV", telemetry.batteryVoltage);
    break;
  case BATTERY_DISPLAY_PERCENT:
  default:
    snprintf(battery_str, sizeof(battery_str), "%d%%", telemetry.batteryPercentage);
    break;
  }

  // 4. Decode footpad sensor state
  bool left_pad = false;
  bool right_pad = false;
  switch (telemetry.switchState) {
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

  switch (preferences.secondary_stat_display) {
  case SECONDARY_STAT_DUTY:
    snprintf(left_val, sizeof(left_val), "%d%%", telemetry.dutyCycle);
    snprintf(left_lbl, sizeof(left_lbl), "DUTY");
    break;
  case SECONDARY_STAT_TEMPS: {
    bool should_convert = preferences.temp_units == TEMP_UNITS_FAHRENHEIT;
    float converted_mot = should_convert ? convert_c_to_f(telemetry.motorTemp) : telemetry.motorTemp;
    float converted_cont = should_convert ? convert_c_to_f(telemetry.controllerTemp) : telemetry.controllerTemp;
    const char *temp_unit = should_convert ? "F" : "C";
    converted_mot = snap_zero(converted_mot, 0.5f);
    converted_cont = snap_zero(converted_cont, 0.5f);
    snprintf(left_val, sizeof(left_val), "%.0f%s|%.0f%s", converted_mot, temp_unit, converted_cont, temp_unit);
    snprintf(left_lbl, sizeof(left_lbl), "TEMP");
    break;
  }
  case SECONDARY_STAT_DISTANCE: {
    float trip_dist = telemetry.tripDistance / 1000.0f;
    if (preferences.distance_units == DISTANCE_UNITS_IMPERIAL) {
      trip_dist = convert_km_to_mi(trip_dist);
    }
    const char *dist_unit = (preferences.distance_units == DISTANCE_UNITS_IMPERIAL) ? "mi" : "km";
    trip_dist = snap_zero(trip_dist, 0.05f);
    snprintf(left_val, sizeof(left_val), "%.1f%s", trip_dist, dist_unit);
    snprintf(left_lbl, sizeof(left_lbl), "TRIP");
    break;
  }
  }

  bool connected = connection_get_state() == CONNECTION_STATE_CONNECTED;
  const char *state_label = get_connection_state_label();

  // Pocket mode active
  bool pocket_mode_active = is_pocket_mode_enabled();

  // Board state message
  bool should_show_board_state =
      connection_get_state() == CONNECTION_STATE_CONNECTED &&
      (telemetry.state == BOARD_STATE_RUNNING_FLYWHEEL || telemetry.state == BOARD_STATE_RUNNING_TILTBACK ||
       telemetry.state == BOARD_STATE_RUNNING_UPSIDEDOWN || telemetry.state == BOARD_STATE_RUNNING_WHEELSLIP);

  const char *board_state_msg = "";
  if (should_show_board_state) {
    switch (telemetry.state) {
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
  slint::SharedString slint_speed_unit = (preferences.distance_units == DISTANCE_UNITS_IMPERIAL) ? "MPH" : "KPH";
  slint::SharedString slint_state_label = state_label;
  slint::SharedString slint_left_val = left_val;
  slint::SharedString slint_left_lbl = left_lbl;
  slint::SharedString slint_battery_str = battery_str;
  slint::SharedString slint_board_state_message = board_state_msg;

  // Push to Slint UI state
  slint::invoke_from_event_loop([=]() {
    ui_update_pending.store(false);

    const auto &state = get_slint_window()->global<UiState>();

    update_stats_brightness(false);

    state.set_speed(slint_speed_str);
    state.set_speed_unit(slint_speed_unit);
    static float last_speed_fraction = -1.0f;
    static float last_utilization_fraction = -1.0f;
    if (arc_fraction_moved(speed_fraction, &last_speed_fraction)) {
      state.set_speed_fraction(last_speed_fraction);
    }
    if (arc_fraction_moved(utilization_fraction, &last_utilization_fraction)) {
      state.set_utilization_fraction(last_utilization_fraction);
    }
    state.set_left_pad(left_pad);
    state.set_right_pad(right_pad);
    state.set_remote_battery(telemetry.remoteBatteryPercentage);
    state.set_remote_charging(telemetry.chargeState == CHARGE_STATE_CHARGING ||
                              telemetry.chargeState == CHARGE_STATE_DONE);
    state.set_is_connected(connected);
    state.set_connection_state((int)connection_get_state());
    state.set_connection_state_label(slint_state_label);
    state.set_secondary_stat_left_value(slint_left_val);
    state.set_secondary_stat_left_label(slint_left_lbl);
    state.set_board_battery(slint_battery_str);
    state.set_rssi(telemetry.signalStrength);
    state.set_pocket_mode_active(pocket_mode_active);
    state.set_lights_on(false);
    state.set_board_state_message(slint_board_state_message);
    state.set_vehicle_type(telemetry.vehicleType);
  });
}

// Double press navigates to Menu Screen
static void double_press_handler() {
  if (settings_get_device().double_press_action == DOUBLE_PRESS_ACTION_OPEN_MENU) {
    slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Menu); });
  }
}

static void update_stats_brightness(bool immediate) {
  if (!get_slint_window() || get_slint_window()->global<UiState>().get_screen() != Screen::Stats)
    return;
  const auto preferences = settings_get_device();
  const bool dialog = get_slint_window()->global<UiState>().get_show_confirm_dialog() || ui_processing_active();
  const uint8_t level = pose_brightness_level(preferences.dimming_mode, preferences.bl_level, stats_raised,
                                              is_pocket_mode_enabled(), dialog, IMU_ENABLED);
  const bool hbm = pose_brightness_hbm(preferences.dimming_mode, stats_raised, is_pocket_mode_enabled(), dialog,
                                       IMU_ENABLED, display_supports_hbm());
  // Dialogs, explicit setting changes and disabling dimming must restore normal
  // brightness immediately rather than waiting out the pose bounce guard.
  if (immediate || dialog || is_pocket_mode_enabled() || preferences.dimming_mode == DIMMING_MODE_OFF)
    brightness_switch_reset(&pose_brightness_switch);
  // 256 represents panel HBM, outside the normal 0..255 brightness range.
  const uint16_t current = display_get_hbm() ? 256 : display_get_bl_level();
  const uint16_t requested = hbm ? 256 : level;
  if (brightness_switch_should_change(&pose_brightness_switch, current, requested, esp_timer_get_time() / 1000) ||
      (current == requested && display_get_bl_level() != level)) {
    display_set_bl_level(level);
    if (display_get_hbm() != hbm)
      display_set_hbm(hbm);
  }
}

extern "C" void stats_screen_refresh_brightness() {
  update_stats_brightness(true);
}

static void apply_imu_pose(bool raised) {
  stats_raised = raised;
  update_stats_brightness(false);
  if (raised && settings_get_device().dimming_mode != DIMMING_MODE_OFF && !is_pocket_mode_enabled() &&
      !get_slint_window()->global<UiState>().get_show_confirm_dialog() && !ui_processing_active()) {
    reset_sleep_timer();
  }
}

// IMU callbacks run on the sensor worker. Apply display/UI policy on the
// event loop and discard work queued before leaving the Stats screen.
extern "C" void handle_imu_gesture(imu_gesture_t gesture) {
  if (gesture == IMU_GESTURE_DOUBLE_TAP) {
    reset_sleep_timer();
    return;
  }
  if (gesture != IMU_GESTURE_RAISED && gesture != IMU_GESTURE_LOWERED)
    return;
  static std::atomic<bool> latest_raised{false};
  static std::atomic<bool> pending{false};
  const uint32_t session = stats_session.load();
  latest_raised.store(gesture == IMU_GESTURE_RAISED);
  if (pending.exchange(true))
    return;
  slint::invoke_from_event_loop([session]() {
    pending.store(false);
    if (session != stats_session.load() || !is_stats_screen_active() || !get_slint_window())
      return;
    apply_imu_pose(latest_raised.load());
  });
}

// Registration functions called from main flow (replaces LVGL loaded events)
extern "C" void setup_stats_properties() {
  stats_session.fetch_add(1);
  stats_raised = false;
  brightness_switch_reset(&pose_brightness_switch);
  update_stats_brightness(true);
  // The provisional lowered entry state must not delay the first real pose.
  brightness_switch_reset(&pose_brightness_switch);
  ui_update_pending.store(false);
  stats_register_update_cb(stats_update_screen_display);
  input_router_claim(INPUT_ACTION_DOUBLE_PRESS, double_press_handler, INPUT_ONCE);
  // The stick is throttle here, not focus
  input_router_claim(INPUT_ACTION_STICK_UP, NULL, INPUT_ONCE);
  input_router_claim(INPUT_ACTION_STICK_DOWN, NULL, INPUT_ONCE);
  input_router_claim_board_forwarding();
  imu_register_gesture_callback(handle_imu_gesture);

  if (get_slint_window()) {
    const auto &state = get_slint_window()->global<UiState>();
    state.on_board_battery_clicked([]() {
      // Cycle board battery format
      if (settings_get_device().battery_display == BATTERY_DISPLAY_PERCENT) {
        settings_set_battery_display(BATTERY_DISPLAY_VOLTAGE);
      }
      else {
        settings_set_battery_display(BATTERY_DISPLAY_PERCENT);
      }
      stats_update_screen_display();
      ui_save_quietly([]() { return save_device_settings(); });
    });

    state.on_secondary_stat_left_clicked([]() {
      // Cycle secondary stat
      settings_set_secondary_stat_display(
          (SecondaryStatDisplayOption)((settings_get_device().secondary_stat_display + 1) % 3));
      stats_update_screen_display();
      ui_save_quietly([]() { return save_device_settings(); });
    });
  }

  stats_update_screen_display();
}

extern "C" void teardown_stats_properties() {
  stats_session.fetch_add(1);
  ui_save_quietly_flush();
  stats_unregister_update_cb(stats_update_screen_display);
  imu_unregister_gesture_callback(handle_imu_gesture);
  display_set_hbm(settings_get_device().dimming_mode == DIMMING_MODE_HBM_ALWAYS);
  display_set_bl_level(settings_get_device().bl_level);
}

extern "C" void handle_splash_exit() {
  ESP_LOGI(TAG, "Splash screen finished");
  slint::invoke_from_event_loop([]() {
    const auto &state = get_slint_window()->global<UiState>();
    if (state.get_screen() != Screen::Splash) {
      return;
    }
    charge_screen_finish_boot();
    if (state.get_screen() == Screen::Splash)
      state.set_screen(Screen::Stats);
  });
}

extern "C" void handle_menu_back() {
  ESP_LOGI(TAG, "Menu back pressed");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Stats); });
}

extern "C" void handle_stats_swiped_down() {
  ESP_LOGI(TAG, "Stats swiped down");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Menu); });
}
