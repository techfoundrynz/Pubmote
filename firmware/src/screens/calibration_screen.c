#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "remote/screen.h"
#include "utilities/screen_utils.h"
#include <colors.h>
#include <remote/display.h>
#include <remote/remoteinputs.h>
#include <remote/settings.h>
#include <screens/calibration_screen.h>
#include <ui/ui.h>

static const char *TAG = "PUBREMOTE-CALIBRATION_SCREEN";

static uint8_t calibration_step = 0;
// Current preview data
static CalibrationSettings calibration_data;
typedef struct {
  uint16_t x_min;
  uint16_t x_max;
  uint16_t y_min;
  uint16_t y_max;
} MinMaxData;

// Min/max data for step calibration
static MinMaxData min_max_data;
static uint16_t deadband = STICK_DEADBAND;
static float expo = STICK_EXPO;

void reset_min_max_data() {
  min_max_data.x_min = STICK_MAX_VAL; // inverted min/max so we include all values
  min_max_data.x_max = STICK_MIN_VAL;
  min_max_data.y_min = STICK_MAX_VAL;
  min_max_data.y_max = STICK_MIN_VAL;
}

bool is_calibration_screen_active() {
  lv_obj_t *active_screen = lv_scr_act();
  return active_screen == ui_CalibrationScreen;
}

static void update_display_stick_label(float x, float y) {
  char *formattedString;
  if (calibration_step == CALIBRATION_STEP_EXPO) {
    asprintf(&formattedString, "Expo: %.2f", expo);
  }
  else {
    asprintf(&formattedString, "X: %.2f \nY: %.2f", x, y);
  }
  lv_label_set_text(ui_CalibrationHeaderLabel, formattedString);
  free(formattedString);
}

// Structure to represent a point
typedef struct {
  double x;
  double y;
} Point;

// Function to calculate a point within a circle
Point calculatePoint(float radius, float x, float y) {
  // Calculate the angle from the center to the point
  double angle = atan2(y, x);

  // Calculate the distance from the center to the point
  double distance = sqrt(x * x + y * y);

  // Adjust the distance to be within the radius
  if (distance > radius) {
    distance = radius;
  }

  // Calculate the x and y coordinates
  double xCoord = distance * cos(angle);
  double yCoord = distance * sin(angle);

  // Create a Point structure and return it
  Point point = {xCoord, yCoord};
  return point;
}

static void update_display_stick_position(float x, float y) {
  lv_coord_t width = lv_obj_get_width(ui_CalibrationIndicatorContainer);
  int radius = width / 2;
  Point position = calculatePoint(radius, x * radius, y * radius);
  lv_obj_set_pos(ui_PositionIndicatorContainer, position.x, -position.y);
}

int16_t get_deadband() {
  int16_t x1_diff = min_max_data.x_max - calibration_data.x_center;
  int16_t y1_diff = min_max_data.y_max - calibration_data.y_center;
  int16_t x2_diff = calibration_data.x_center - min_max_data.x_min;
  int16_t y2_diff = calibration_data.y_center - min_max_data.y_min;

  // return highest of all diffs
  int16_t x_diff = (x1_diff > x2_diff ? x1_diff : x2_diff);
  int16_t y_diff = (y1_diff > y2_diff ? y1_diff : y2_diff);

  return x_diff > y_diff ? x_diff : y_diff;
}

static void update_deadband_indicator() {
  if (calibration_step == CALIBRATION_STEP_DEADBAND) {
    int16_t new_deadband = get_deadband();
    uint16_t x_diff = calibration_data.x_max - calibration_data.x_min;
    uint16_t y_diff = calibration_data.y_max - calibration_data.y_min;
    uint8_t deadband_pct_x = (int8_t)(((float)new_deadband / (float)x_diff) * 100);
    uint8_t deadband_pct_y = (int8_t)(((float)new_deadband / (float)y_diff) * 100);
    uint8_t deadband_pct = (deadband_pct_x > deadband_pct_y ? deadband_pct_x : deadband_pct_y);
    lv_obj_set_width(ui_DeadbandIndicator, lv_pct(deadband_pct * 2));
    lv_obj_set_height(ui_DeadbandIndicator, lv_pct(deadband_pct * 2));

    deadband = new_deadband;
  }
}

static void update_min_max() {
  if (joystick_data.x < min_max_data.x_min) {
    min_max_data.x_min = joystick_data.x;
  }
  if (joystick_data.x > min_max_data.x_max) {
    min_max_data.x_max = joystick_data.x;
  }

  if (joystick_data.y < min_max_data.y_min) {
    min_max_data.y_min = joystick_data.y;
  }
  if (joystick_data.y > min_max_data.y_max) {
    min_max_data.y_max = joystick_data.y;
  }
}

static void update_stick_press_indicator() {
  bool is_stick_down = gpio_get_level(PRIMARY_BUTTON) == JOYSTICK_BUTTON_LEVEL;
  if (is_stick_down) {
    lv_obj_set_style_bg_color(ui_PositionIndicatorHoriz, lv_color_hex(COLOR_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_PositionIndicatorVert, lv_color_hex(COLOR_PRIMARY), LV_PART_MAIN);
  }
  else {
    lv_obj_set_style_bg_color(ui_PositionIndicatorHoriz, lv_color_hex(COLOR_PRIMARY_TEXT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_PositionIndicatorVert, lv_color_hex(COLOR_PRIMARY_TEXT), LV_PART_MAIN);
  }
}

void calibration_task(void *pvParameters) {
  while (is_calibration_screen_active()) {
    update_min_max();

    if (LVGL_lock(-1)) {
// Get values using current calibration data
#if JOYSTICK_X_ENABLED
      float curr_x_val =
          convert_adc_to_axis(joystick_data.x, calibration_data.x_min, calibration_data.x_center,
                              calibration_data.x_max, calibration_data.deadband, calibration_data.expo, false);
#else
      float curr_x_val = 0;
#endif

#if JOYSTICK_Y_ENABLED
      float curr_y_val = convert_adc_to_axis(joystick_data.y, calibration_data.y_min, calibration_data.y_center,
                                             calibration_data.y_max, calibration_data.deadband, calibration_data.expo,
                                             calibration_data.invert_y);
#else
      float curr_y_val = 0;
#endif

      update_display_stick_label(curr_x_val, curr_y_val);
      update_display_stick_position(curr_x_val, curr_y_val);
      update_deadband_indicator();
      update_stick_press_indicator();

      LVGL_unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(LV_DEF_REFR_PERIOD));
  }

  ESP_LOGI(TAG, "Calibration task ended");
  vTaskDelete(NULL);
}

void update_calibration_screen() {
  if (LVGL_lock(0)) {
    lv_obj_clear_flag(ui_CalibrationIndicatorContainer, LV_OBJ_FLAG_HIDDEN); // show on every step except expo
    lv_obj_add_flag(ui_DeadbandIndicator, LV_OBJ_FLAG_HIDDEN);               // Hide for every step except deadband
    lv_obj_add_flag(ui_ExpoSlider, LV_OBJ_FLAG_HIDDEN);                      // Hide for every step except expo
    lv_obj_add_flag(ui_StickFlags, LV_OBJ_FLAG_HIDDEN);                      // Hide for every step except flags

    switch (calibration_step) {
    case CALIBRATION_STEP_START:
      lv_label_set_text(ui_CalibrationStepLabel, "Press start to begin calibration");
      lv_label_set_text(ui_CalibrationPrimaryActionButtonLabel, "Start");
      break;
    case CALIBRATION_STEP_CENTER:
      lv_label_set_text(ui_CalibrationStepLabel, "Move stick to center");
      lv_label_set_text(ui_CalibrationPrimaryActionButtonLabel, "Next");
      break;
    case CALIBRATION_STEP_MINMAX:
      lv_label_set_text(ui_CalibrationStepLabel, "Move stick to min/max");
      lv_label_set_text(ui_CalibrationPrimaryActionButtonLabel, "Next");
      break;
    case CALIBRATION_STEP_DEADBAND:
      lv_label_set_text(ui_CalibrationStepLabel, "Move stick within deadband");
      lv_label_set_text(ui_CalibrationPrimaryActionButtonLabel, "Next");
      lv_obj_set_width(ui_DeadbandIndicator, lv_pct(0));
      lv_obj_set_height(ui_DeadbandIndicator, lv_pct(0));
      lv_obj_clear_flag(ui_DeadbandIndicator, LV_OBJ_FLAG_HIDDEN);
      break;
    case CALIBRATION_STEP_EXPO:
      lv_slider_set_value(ui_ExpoSlider, (int)(calibration_data.expo * 10), LV_ANIM_OFF);
      lv_obj_add_flag(ui_CalibrationIndicatorContainer, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(ui_CalibrationStepLabel, "Set expo factor");
      lv_label_set_text(ui_CalibrationPrimaryActionButtonLabel, "Next");
      lv_obj_clear_flag(ui_ExpoSlider, LV_OBJ_FLAG_HIDDEN);
      break;
    case CALIBRATION_STEP_STICK_FLAGS:
      lv_obj_add_flag(ui_CalibrationIndicatorContainer, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(ui_CalibrationStepLabel, "Axis options");
      lv_label_set_text(ui_CalibrationPrimaryActionButtonLabel, "Next");
      lv_obj_clear_flag(ui_StickFlags, LV_OBJ_FLAG_HIDDEN);

      if (calibration_data.invert_y) {
        lv_obj_add_state(ui_InvertYSwitch, LV_STATE_CHECKED);
      }
      else {
        lv_obj_clear_state(ui_InvertYSwitch, LV_STATE_CHECKED);
      }

      break;
    case CALIBRATION_STEP_DONE:
      lv_label_set_text(ui_CalibrationStepLabel, "Calibration complete!");
      lv_label_set_text(ui_CalibrationPrimaryActionButtonLabel, "Save");
      break;
    }
    LVGL_unlock();
  }
}

static void reset_calibration_data() {
  calibration_data.x_center = STICK_MID_VAL;
  calibration_data.y_center = STICK_MID_VAL;
  calibration_data.x_min = STICK_MIN_VAL;
  calibration_data.y_min = STICK_MIN_VAL;
  calibration_data.x_max = STICK_MAX_VAL;
  calibration_data.y_max = STICK_MAX_VAL;
  calibration_data.deadband = STICK_DEADBAND;
  calibration_data.expo = STICK_EXPO;
  // calibration_data.invert_x = INVERT_X_AXIS;
  calibration_data.invert_y = INVERT_Y_AXIS;
  // calibration_data.invert_xy = INVERT_XY_AXIS;
}

static void load_current_calibration_data() {
  calibration_data.x_center = calibration_settings.x_center;
  calibration_data.y_center = calibration_settings.y_center;
  calibration_data.x_min = calibration_settings.x_min;
  calibration_data.y_min = calibration_settings.y_min;
  calibration_data.x_max = calibration_settings.x_max;
  calibration_data.y_max = calibration_settings.y_max;
  calibration_data.deadband = calibration_settings.deadband;
  calibration_data.expo = calibration_settings.expo;
  calibration_data.invert_y = calibration_settings.invert_y;
}

// Event handlers
void calibration_screen_load_start(lv_event_t *e) {
  ESP_LOGI(TAG, "Calibration screen load start");
  calibration_step = 0;

  if (LVGL_lock(0)) {
    apply_ui_scale(NULL);
    create_navigation_group(ui_CalibrationFooter);
    LVGL_unlock();
  }
  update_calibration_screen();
}

void calibration_screen_loaded(lv_event_t *e) {
  ESP_LOGI(TAG, "Calibration screen loaded");

  load_current_calibration_data();
  reset_min_max_data();
  deadband = STICK_DEADBAND;
  expo = STICK_EXPO;
  update_calibration_screen();

  // Start task to update UI
  xTaskCreate(calibration_task, "calibration_task", 4096, NULL, 2, NULL);
}

void calibration_screen_unload_start(lv_event_t *e) {
  ESP_LOGI(TAG, "Calibration screen unload start");
}

// Event handlers
void calibration_settings_primary_button_press(lv_event_t *e) {
  if (calibration_step == CALIBRATION_STEP_START) {
    reset_calibration_data();
  }
  else if (calibration_step == CALIBRATION_STEP_CENTER) {
    calibration_data.x_center = joystick_data.x;
    calibration_data.y_center = joystick_data.y;
  }
  else if (calibration_step == CALIBRATION_STEP_MINMAX) {
    calibration_data.x_min = min_max_data.x_min;
    calibration_data.x_max = min_max_data.x_max;
    calibration_data.y_min = min_max_data.y_min;
    calibration_data.y_max = min_max_data.y_max;
  }
  else if (calibration_step == CALIBRATION_STEP_DEADBAND) {
    calibration_data.deadband = deadband;
  }
  else if (calibration_step == CALIBRATION_STEP_EXPO) {
    calibration_data.expo = expo;
  }
  else if (calibration_step == CALIBRATION_STEP_STICK_FLAGS) {
    calibration_data.invert_y = lv_obj_has_state(ui_InvertYSwitch, LV_STATE_CHECKED);
  }
  else if (calibration_step >= CALIBRATION_STEP_DONE) {
    calibration_settings = calibration_data;
    save_calibration();
    _ui_screen_change(&ui_MenuScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, &ui_MenuScreen_screen_init);
    return;
  }

  calibration_step++;
  // Reset step data each time we move to a new step
  reset_min_max_data();
  deadband = STICK_DEADBAND;
  expo = STICK_EXPO;

  update_calibration_screen();
}

void calibration_settings_secondary_button_press(lv_event_t *e) {
  // change to menu screen with 200ms move left animation
}

void expo_slider_change(lv_event_t *e) {
  float val = (float)lv_slider_get_value(ui_ExpoSlider);
  expo = val / 10;
}