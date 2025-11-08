#include "screens/stats_screen.h"
#include "core/lv_obj_event.h"
#include "esp_log.h"
#include "remote/display.h"
#include "remote/remoteinputs.h"
#include "remote/vehicle_state.h"
#include "utilities/screen_utils.h"
#include <colors.h>
#include <math.h>
#include <remote/connection.h>
#include <remote/settings.h>
#include <remote/stats.h>
#include <string.h>
#include <utilities/conversion_utils.h>

static const char *TAG = "PUBREMOTE-STATS_SCREEN";

StatsScreenDisplayOptions stat_display_options = {
    .primary_stat = STAT_DISPLAY_SPEED,
    .secondary_stat = STAT_DISPLAY_DUTY,
};

static void change_stat_display(int direction) {
  if (direction > 0) {
    stat_display_options.primary_stat = (stat_display_options.primary_stat + 1) % 4;
  }
  else {
    stat_display_options.primary_stat = (stat_display_options.primary_stat + 3) % 4;
  }
}

bool is_stats_screen_active() {
  lv_obj_t *active_screen = lv_scr_act();
  return active_screen == ui_StatsScreen;
}

static uint8_t max_speed = 0;

static void update_speed_dial_display() {
  static float last_value = 0;

  // Ensure the value has changed
  if (last_value == remoteStats.speed) {
    return;
  }

  if (!max_speed) {
    /// get range from arc in case it was not set
    max_speed = lv_arc_get_max_value(ui_SpeedDial);
  }

  if (remoteStats.speed > max_speed) {
    max_speed = (uint8_t)remoteStats.speed;
    // Set range based on max speed

    lv_arc_set_range(ui_SpeedDial, 0, max_speed);
    // lv_bar_set_range(ui_SpeedBar, 0, max_speed);
  }

  lv_arc_set_value(ui_SpeedDial, remoteStats.speed);
  // lv_bar_set_value(ui_SpeedBar, remoteStats.speed, LV_ANIM_OFF);

  // Update the last value
  last_value = remoteStats.speed;
}

static void update_utilization_dial_display() {
  static uint8_t last_value = 0;

  // Ensure the value has changed
  if (last_value == remoteStats.dutyCycle) {
    return;
  }

  lv_arc_set_value(ui_UtilizationDial, remoteStats.dutyCycle);

  // set arc color
  lv_color_t color = lv_color_hex(COLOR_ACTIVE);

  DutyStatus duty_status = get_duty_status(remoteStats.dutyCycle);

  if (duty_status != DUTY_STATUS_NONE) {
    color = lv_color_hex(get_duty_color(duty_status));
  }

  lv_obj_set_style_arc_color(ui_UtilizationDial, color, LV_PART_INDICATOR | LV_STATE_DEFAULT);

  // Update the last value
  last_value = remoteStats.dutyCycle;
}

static void update_remote_battery_display() {
  static uint8_t last_remote_battery_value = 0;

  // Ensure the value has changed
  if (last_remote_battery_value == remoteStats.remoteBatteryPercentage) {
    return;
  }

  // Set background to red below 20%
  if (remoteStats.remoteBatteryPercentage < 20 && remoteStats.remoteBatteryPercentage != 0) {
    lv_obj_set_style_bg_color(ui_BatteryFill, lv_color_hex(0xb20000), LV_PART_MAIN | LV_STATE_DEFAULT);
  }
  else {
    lv_obj_set_style_bg_color(ui_BatteryFill, lv_color_hex(0x1db200), LV_PART_MAIN | LV_STATE_DEFAULT);
  }

  // Set width of battery object
  lv_obj_set_width(ui_BatteryFill, lv_pct(remoteStats.remoteBatteryPercentage));

  // Update the last value
  last_remote_battery_value = remoteStats.remoteBatteryPercentage;
}

static void update_rssi_display() {
  // Use derived values to avoid unnecessary updates
  static uint8_t last_signal_strength_rating_value = SIGNAL_STRENGTH_NONE;
  SignalStrength signal_strength_rating = SIGNAL_STRENGTH_NONE;

  if (remoteStats.signalStrength > RSSI_GOOD) {
    signal_strength_rating = SIGNAL_STRENGTH_GOOD;
  }
  else if (remoteStats.signalStrength > RSSI_FAIR) {
    signal_strength_rating = SIGNAL_STRENGTH_FAIR;
  }
  else if (remoteStats.signalStrength > RSSI_POOR) {
    signal_strength_rating = SIGNAL_STRENGTH_POOR;
  }
  else {
    signal_strength_rating = SIGNAL_STRENGTH_NONE;
  }

  // Hide RSSI container on disconnect if not already hidden
  // Otherwise, show the container if not already shown
  if (connection_state == CONNECTION_STATE_DISCONNECTED && !lv_obj_has_flag(ui_RSSIContainer, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_add_flag(ui_RSSIContainer, LV_OBJ_FLAG_HIDDEN);
  }
  else if (connection_state != CONNECTION_STATE_DISCONNECTED && lv_obj_has_flag(ui_RSSIContainer, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_clear_flag(ui_RSSIContainer, LV_OBJ_FLAG_HIDDEN);
  }

  // Ensure the value has changed
  if (last_signal_strength_rating_value == signal_strength_rating) {
    return;
  }

  int bar_color[] = {RSSI_BAR_OFF, RSSI_BAR_OFF, RSSI_BAR_OFF};

  for (int i = 0; i < 3; i++) {
    if (signal_strength_rating >= i) {
      bar_color[i] = RSSI_BAR_ON;
    }
    else {
      bar_color[i] = RSSI_BAR_OFF;
    }
  }

  lv_obj_set_style_bg_color(ui_RSSI1, lv_color_hex(bar_color[0]), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(ui_RSSI2, lv_color_hex(bar_color[1]), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(ui_RSSI3, lv_color_hex(bar_color[2]), LV_PART_MAIN | LV_STATE_DEFAULT);

  // Update the last value
  last_signal_strength_rating_value = signal_strength_rating;
}

static void update_pocket_mode_display() {
  // Hide pocket mode container on disconnect if not already hidden
  // Otherwise, show the container if not already shown
  if (!is_pocket_mode_enabled() && !lv_obj_has_flag(ui_RemoteModeContainer, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_add_flag(ui_RemoteModeContainer, LV_OBJ_FLAG_HIDDEN);
  }
  else if (is_pocket_mode_enabled() && lv_obj_has_flag(ui_RemoteModeContainer, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_clear_flag(ui_RemoteModeContainer, LV_OBJ_FLAG_HIDDEN);
  }
}

static void update_primary_stat_display() {
  static float last_value = 0;

  // Ensure the value has changed
  if (last_value == remoteStats.speed) {
    return;
  }

  float converted_val = remoteStats.speed;

  if (device_settings.distance_units == DISTANCE_UNITS_IMPERIAL) {
    converted_val = convert_kph_to_mph(remoteStats.speed);
  }

  char *formattedString;

  if (converted_val >= 10) {
    asprintf(&formattedString, "%.0f", converted_val);
  }
  else {
    asprintf(&formattedString, "%.1f", converted_val);
  }

  lv_label_set_text(ui_PrimaryStat, formattedString);
  free(formattedString);

  // Update the last value
  last_value = remoteStats.speed;
}

static char *get_board_state_string(BoardState state) {
  switch (state) {
  case BOARD_STATE_RUNNING_FLYWHEEL:
    return "FLYWHEEL";
  case BOARD_STATE_RUNNING_TILTBACK:
    return "TILTBACK";
  case BOARD_STATE_RUNNING_WHEELSLIP:
    return "WHEELSLIP";
  case BOARD_STATE_RUNNING_UPSIDEDOWN:
    return "UPSIDEDOWN";
  default:
    return "";
  }
}

static void update_board_state_text() {
  static char *last_board_state_string = NULL;

  char *formattedString;
  asprintf(&formattedString, "%s", get_board_state_string(remoteStats.state));

  // First time initialization
  if (last_board_state_string == NULL) {
    last_board_state_string = strdup(formattedString);
    lv_label_set_text(ui_MessageText, formattedString);
    free(formattedString);
    return;
  }

  // Compare and update if different
  if (strcmp(last_board_state_string, formattedString) != 0) {
    free(last_board_state_string);
    last_board_state_string = strdup(formattedString);
    lv_label_set_text(ui_MessageText, formattedString);
  }

  free(formattedString);
}

static void update_header_display() {
  static bool last_should_show_board_state = false;
  bool should_show_board_state =
      connection_state == CONNECTION_STATE_CONNECTED &&
      (remoteStats.state == BOARD_STATE_RUNNING_FLYWHEEL || remoteStats.state == BOARD_STATE_RUNNING_TILTBACK ||
       remoteStats.state == BOARD_STATE_RUNNING_UPSIDEDOWN || remoteStats.state == BOARD_STATE_RUNNING_WHEELSLIP);

  if (should_show_board_state != last_should_show_board_state) {
    if (should_show_board_state) {
      // Show board state tex
      lv_obj_add_flag(ui_RemoteIndicatorContainer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_MessageText, LV_OBJ_FLAG_HIDDEN);
    }
    else {
      // Hide board state text
      lv_obj_add_flag(ui_MessageText, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_RemoteIndicatorContainer, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (should_show_board_state) {
    update_board_state_text();
  }
  else {
    update_remote_battery_display();
    update_rssi_display();
    update_pocket_mode_display();
  }

  last_should_show_board_state = should_show_board_state;
}

static void update_duty_cycle_display() {
  static float last_value = 0;

  // Ensure the value has changed
  if (last_value == remoteStats.dutyCycle) {
    return;
  }

  // Update the displayed text
  char *formattedString;
  asprintf(&formattedString, "Duty Cycle: %d%%", remoteStats.dutyCycle);
  lv_label_set_text(ui_DutyCycleLabel, formattedString);
  free(formattedString);

  // Update the last value
  last_value = remoteStats.dutyCycle;
}

static void update_temps_display() {
  static float last_motor_temp_value = 0.0;
  static float last_controller_temp_value = 0.0;

  // Ensure the value has changed
  if (last_motor_temp_value == remoteStats.motorTemp && last_controller_temp_value == remoteStats.controllerTemp) {
    return;
  }

  bool should_convert = device_settings.temp_units == TEMP_UNITS_FAHRENHEIT;
  float converted_mot_val = remoteStats.motorTemp;
  float converted_cont_val = remoteStats.controllerTemp;
  char temp_unit_label[] = CELSIUS_LABEL;

  if (should_convert) {
    converted_mot_val = convert_c_to_f(remoteStats.motorTemp);
    converted_cont_val = convert_c_to_f(remoteStats.controllerTemp);
    strncpy(temp_unit_label, FAHRENHEIT_LABEL, sizeof(temp_unit_label) - 1);
  }

  // Update the displayed text
  char *formattedString;
  asprintf(&formattedString, "M: %.0f°%s | C: %.0f°%s", converted_mot_val, temp_unit_label, converted_cont_val,
           temp_unit_label);
  lv_label_set_text(ui_TempsLabel, formattedString);
  free(formattedString);

  // Update the last value
  last_motor_temp_value = remoteStats.motorTemp;
  last_controller_temp_value = remoteStats.controllerTemp;
}

static void update_trip_distance_display() {
  static float last_trip_distance_value = -1; // Set to -1 to force initial update
  float new_trip_distance = remoteStats.tripDistance / 1000.0;

  // Ensure the value has changed
  if (last_trip_distance_value == new_trip_distance) {
    return;
  }

  float converted_val = new_trip_distance;

  if (device_settings.distance_units == DISTANCE_UNITS_IMPERIAL) {
    converted_val = convert_kph_to_mph(new_trip_distance);
  }

  char distance_label[] = KILOMETERS_LABEL;

  if (device_settings.distance_units == DISTANCE_UNITS_IMPERIAL) {
    strncpy(distance_label, MILES_LABEL, sizeof(distance_label) - 1);
  }

  // Update the displayed text
  char *formattedString;
  asprintf(&formattedString, "Trip: %.1f%s", converted_val, distance_label);
  lv_label_set_text(ui_TripLabel, formattedString);
  free(formattedString);

  // Update the last value
  last_trip_distance_value = new_trip_distance;
}

static char *get_connection_state_label() {
  switch (connection_state) {
  case CONNECTION_STATE_CONNECTED:
    return "Connected";
  case CONNECTION_STATE_CONNECTING:
    return "Connecting";
  case CONNECTION_STATE_RECONNECTING:
    return "Reconnecting";
  case CONNECTION_STATE_DISCONNECTED:
    return "Disconnected";
  default:
    return "Disconnected";
  }
}

static void update_secondary_stat_display() {
  static ConnectionState last_connection_state = CONNECTION_STATE_DISCONNECTED;
  static lv_coord_t connected_scroll_position = 0;
  ConnectionState new_connection_state = connection_state;

  // See if the current connection state has changed
  // Update shown fields accordingly
  if (last_connection_state != new_connection_state) {
    bool is_connected = new_connection_state == CONNECTION_STATE_CONNECTED;

    // Update available options based on connection state
    if (is_connected) {
      lv_obj_add_flag(ui_ConnectionStateBody, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_DutyCycleBody, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_TempsBody, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_TripBody, LV_OBJ_FLAG_HIDDEN);
      lv_obj_scroll_to(ui_SecondaryStatContainer, connected_scroll_position, 0, LV_ANIM_OFF);
    }
    else {
      connected_scroll_position = lv_obj_get_scroll_x(ui_SecondaryStatContainer);
      lv_obj_clear_flag(ui_ConnectionStateBody, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_DutyCycleBody, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_TempsBody, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_TripBody, LV_OBJ_FLAG_HIDDEN);
      lv_obj_scroll_to(ui_SecondaryStatContainer, 0, 0, LV_ANIM_OFF);
    }

    lv_label_set_text(ui_ConnectionStateLabel, get_connection_state_label());
  }

  // Update secondary stat displays if currently connected
  if (connection_state == CONNECTION_STATE_CONNECTED) {
    update_duty_cycle_display();
    update_temps_display();
    update_trip_distance_display();
  }

  // Update the last value
  last_connection_state = new_connection_state;
}

static void update_board_battery_display() {
  static float last_board_battery_voltage = 0;
  static BoardBatteryDisplayOption last_units = 0;
  char *formattedString;

  // Ensure the value has changed
  if (fabsf(last_board_battery_voltage - remoteStats.batteryVoltage) < 0.1f &&
      device_settings.battery_display == last_units) {
    return;
  }

  switch (device_settings.battery_display) {
  case BATTERY_DISPLAY_PERCENT:
    // Update the displayed text
    asprintf(&formattedString, "%d%%", remoteStats.batteryPercentage);
    break;
  case BATTERY_DISPLAY_VOLTAGE:
    // Update the displayed text
    asprintf(&formattedString, "%.1fV", remoteStats.batteryVoltage);
    break;
  case BATTERY_DISPLAY_ALL:
    // Update the displayed text
    asprintf(&formattedString, "%d%% | %.1fV", remoteStats.batteryPercentage, remoteStats.batteryVoltage);
    break;
  }

  lv_label_set_text(ui_BoardBatteryDisplay, formattedString);
  free(formattedString);

  // Update the last values
  last_board_battery_voltage = remoteStats.batteryVoltage;
  last_units = device_settings.battery_display;
}

static void change_board_battery_display() {
  // Rotate battery display type
  if (device_settings.battery_display == BATTERY_DISPLAY_ALL) {
    device_settings.battery_display = 0;
  }
  else {
    device_settings.battery_display += 1;
  }

  // Save settings
  save_device_settings();

  // Call an immediate update to the battery display to update things
  update_board_battery_display();
}

static void update_footpad_display() {
  static SwitchState last_value = SWITCH_STATE_OFF;

  // Ensure the value has changed
  if (last_value == remoteStats.switchState) {
    return;
  }

  switch (remoteStats.switchState) {
  case SWITCH_STATE_OFF:
    lv_arc_set_value(ui_LeftSensor, 0);
    lv_arc_set_value(ui_RightSensor, 0);
    break;
  case SWITCH_STATE_LEFT:
    lv_arc_set_value(ui_LeftSensor, 1);
    lv_arc_set_value(ui_RightSensor, 0);
    break;
  case SWITCH_STATE_RIGHT:
    lv_arc_set_value(ui_LeftSensor, 0);
    lv_arc_set_value(ui_RightSensor, 1);
    break;
  case SWITCH_STATE_BOTH:
    lv_arc_set_value(ui_LeftSensor, 1);
    lv_arc_set_value(ui_RightSensor, 1);
    break;
  default:
    break;
  }

  last_value = remoteStats.switchState;
}

static void stats_update_screen_display() {
  if (LVGL_lock(LV_DEF_REFR_PERIOD)) {
    if (device_settings.distance_units == DISTANCE_UNITS_METRIC) {
      lv_label_set_text(ui_PrimaryStatUnit, KILOMETERS_PER_HOUR_LABEL);
    }
    else {
      lv_label_set_text(ui_PrimaryStatUnit, MILES_PER_HOUR_LABEL);
    }

    update_speed_dial_display();
    update_utilization_dial_display();
    update_header_display();
    update_primary_stat_display();
    update_secondary_stat_display();
    update_board_battery_display();
    update_footpad_display();

    LVGL_unlock();
  }
}

// Event handlers
void stats_screen_load_start(lv_event_t *e) {
  ESP_LOGI(TAG, "Stats screen load start");
  // Permanent screen - don't apply scale

  if (LVGL_lock(-1)) {
#if (UI_SHAPE == 1)
  // Rectangle UI
  // lv_obj_add_flag(ui_SpeedDial, LV_OBJ_FLAG_HIDDEN);
  // lv_obj_add_flag(ui_UtilizationDial, LV_OBJ_FLAG_HIDDEN);
  // lv_obj_clear_flag(ui_SpeedBar, LV_OBJ_FLAG_HIDDEN);
  // lv_obj_clear_flag(ui_UtilizationBar, LV_OBJ_FLAG_HIDDEN);
  #error "Rectangle UI not supported"
#else
    // Circle UI
    // lv_obj_add_flag(ui_SpeedBar, LV_OBJ_FLAG_HIDDEN);
    // lv_obj_add_flag(ui_UtilizationBar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_SpeedDial, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_UtilizationDial, LV_OBJ_FLAG_HIDDEN);

#endif
    create_navigation_group(ui_StatsContent);
    LVGL_unlock();
  }

  stats_register_update_cb(stats_update_screen_display);
}

static bool double_press_handler() {
  if (device_settings.double_press_action == DOUBLE_PRESS_ACTION_OPEN_MENU) {
    // Open the main menu
    if (LVGL_lock(-1)) {
      _ui_screen_change(&ui_MenuScreen, LV_SCR_LOAD_ANIM_OVER_TOP, 200, 0, &ui_MenuScreen_screen_init);
      LVGL_unlock();
    }
  }

  return true;
}

void stats_screen_loaded(lv_event_t *e) {
  ESP_LOGI(TAG, "Stats screen loaded");
  stats_update_screen_display();
  register_primary_button_cb(BUTTON_EVENT_DOUBLE_PRESS, double_press_handler);

  if (LVGL_lock(-1)) {
    lv_obj_set_scroll_snap_x(ui_SecondaryStatContainer, LV_SCROLL_SNAP_CENTER);
    LVGL_unlock();
  }
}

void stats_screen_unload_start(lv_event_t *e) {
  ESP_LOGI(TAG, "Stats screen unload start");
  stats_unregister_update_cb(stats_update_screen_display);
  unregister_primary_button_cb(BUTTON_EVENT_DOUBLE_PRESS);
}

void stat_long_press(lv_event_t *e) {
  change_stat_display(1);
}

void stat_swipe_left(lv_event_t *e) {
  change_stat_display(1);
}

void stat_swipe_right(lv_event_t *e) {
  change_stat_display(-1);
}

void stats_footer_long_press(lv_event_t *e) {
  change_board_battery_display();
}
