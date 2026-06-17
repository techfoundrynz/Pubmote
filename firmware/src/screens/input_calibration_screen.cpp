#include "screens/input_calibration_screen.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "generated/app-window.h"
#include "remote/display.h"
#include "remote/remoteinputs.h"
#include "remote/settings.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-INPUT_CALIBRATION_SCREEN";

#define CALIBRATION_STEP_START 0
#define CALIBRATION_STEP_CENTER 1
#define CALIBRATION_STEP_MINMAX 2
#define CALIBRATION_STEP_DEADBAND 3
#define CALIBRATION_STEP_EXPO 4
#define CALIBRATION_STEP_STICK_FLAGS 5
#define CALIBRATION_STEP_DONE 6

static TaskHandle_t calibration_task_handle = NULL;
static uint8_t calibration_step = 0;
static CalibrationSettings calibration_data;

struct MinMaxData {
  uint16_t x_min;
  uint16_t x_max;
  uint16_t y_min;
  uint16_t y_max;
};

static MinMaxData min_max_data;
static uint16_t deadband = STICK_DEADBAND;
static float expo = STICK_EXPO;

static void reset_min_max_data() {
  min_max_data.x_min = STICK_MAX_VAL;
  min_max_data.x_max = STICK_MIN_VAL;
  min_max_data.y_min = STICK_MAX_VAL;
  min_max_data.y_max = STICK_MIN_VAL;
}

static int16_t get_deadband() {
  int16_t x1_diff = min_max_data.x_max - calibration_data.x_center;
  int16_t y1_diff = min_max_data.y_max - calibration_data.y_center;
  int16_t x2_diff = calibration_data.x_center - min_max_data.x_min;
  int16_t y2_diff = calibration_data.y_center - min_max_data.y_min;

  int16_t x_diff = (x1_diff > x2_diff ? x1_diff : x2_diff);
  int16_t y_diff = (y1_diff > y2_diff ? y1_diff : y2_diff);

  return x_diff > y_diff ? x_diff : y_diff;
}

static void update_calibration_ui_strings() {
  if (!get_slint_window())
    return;

  const char *step_label = "";
  const char *primary_label = "Next";

  switch (calibration_step) {
  case CALIBRATION_STEP_START:
    step_label = "Press start to begin calibration";
    primary_label = "Start";
    break;
  case CALIBRATION_STEP_CENTER:
    step_label = "Move stick to center";
    break;
  case CALIBRATION_STEP_MINMAX:
    step_label = "Move stick to min/max";
    break;
  case CALIBRATION_STEP_DEADBAND:
    step_label = "Move stick within deadband";
    break;
  case CALIBRATION_STEP_EXPO:
    step_label = "Set expo factor";
    break;
  case CALIBRATION_STEP_STICK_FLAGS:
    step_label = "Axis options (Invert Y)";
    break;
  case CALIBRATION_STEP_DONE:
    step_label = "Calibration complete!";
    primary_label = "Save";
    break;
  }

  slint::invoke_from_event_loop([=]() {
    const auto &state = get_slint_window()->global<UiState>();
    state.set_input_calibration_step(step_label);
    state.set_input_calibration_primary_text(primary_label);
    state.set_show_expo_slider(calibration_step == CALIBRATION_STEP_EXPO);
    state.set_show_invert_switch(calibration_step == CALIBRATION_STEP_STICK_FLAGS);
    if (calibration_step == CALIBRATION_STEP_EXPO) {
      state.set_expo_value(calibration_data.expo);
    }
    if (calibration_step == CALIBRATION_STEP_STICK_FLAGS) {
      state.set_invert_y(calibration_data.invert_y);
    }
  });
}

static void calibration_task(void *pvParameters) {
  while (is_input_calibration_screen_active()) {
    // 1. Update min/max ranges
    if (joystick_data.x < min_max_data.x_min)
      min_max_data.x_min = joystick_data.x;
    if (joystick_data.x > min_max_data.x_max)
      min_max_data.x_max = joystick_data.x;
    if (joystick_data.y < min_max_data.y_min)
      min_max_data.y_min = joystick_data.y;
    if (joystick_data.y > min_max_data.y_max)
      min_max_data.y_max = joystick_data.y;

    // 2. Convert ADC to current axis values (-1.0 to 1.0)
    float curr_x = 0;
#if JOYSTICK_X_ENABLED
    curr_x = convert_adc_to_axis(joystick_data.x, calibration_data.x_min, calibration_data.x_center,
                                 calibration_data.x_max, calibration_data.deadband, calibration_data.expo, false);
#endif

    float curr_y = 0;
#if JOYSTICK_Y_ENABLED
    curr_y =
        convert_adc_to_axis(joystick_data.y, calibration_data.y_min, calibration_data.y_center, calibration_data.y_max,
                            calibration_data.deadband, calibration_data.expo, calibration_data.invert_y);
#endif

    // 3. Format header label
    char header_str[64];
    if (calibration_step == CALIBRATION_STEP_EXPO) {
      if (get_slint_window()) {
        expo = get_slint_window()->global<UiState>().get_expo_value();
      }
      snprintf(header_str, sizeof(header_str), "Expo: %.2f", expo);
    }
    else {
      snprintf(header_str, sizeof(header_str), "X: %.2f | Y: %.2f", curr_x, curr_y);
    }

    if (calibration_step == CALIBRATION_STEP_DEADBAND) {
      deadband = get_deadband();
    }

    // 4. Update Slint properties
    slint::invoke_from_event_loop([=]() {
      const auto &state = get_slint_window()->global<UiState>();
      state.set_input_calibration_readout(header_str);
      state.set_joystick_x(curr_x);
      state.set_joystick_y(curr_y); // Slint dial moves down on positive Y, matching joystick inversion
    });

    vTaskDelay(pdMS_TO_TICKS(30));
  }

  ESP_LOGI(TAG, "Calibration task ended");
  calibration_task_handle = NULL;
  vTaskDelete(NULL);
}

extern "C" void setup_input_calibration_properties() {
  calibration_step = CALIBRATION_STEP_START;

  // Load current values
  calibration_data.x_center = calibration_settings.x_center;
  calibration_data.y_center = calibration_settings.y_center;
  calibration_data.x_min = calibration_settings.x_min;
  calibration_data.y_min = calibration_settings.y_min;
  calibration_data.x_max = calibration_settings.x_max;
  calibration_data.y_max = calibration_settings.y_max;
  calibration_data.deadband = calibration_settings.deadband;
  calibration_data.expo = calibration_settings.expo;
  calibration_data.invert_y = calibration_settings.invert_y;

  reset_min_max_data();
  deadband = STICK_DEADBAND;
  expo = STICK_EXPO;

  update_calibration_ui_strings();

  if (calibration_task_handle == NULL) {
    xTaskCreate(calibration_task, "calibration_task", 4096, NULL, 2, &calibration_task_handle);
  }
}

// Slint Event callbacks
extern "C" void handle_input_calibration_primary() {
  ESP_LOGI(TAG, "Calibration primary action at step %d", calibration_step);

  if (calibration_step == CALIBRATION_STEP_START) {
    // Reset calibration parameters to default
    calibration_data.x_center = STICK_MID_VAL;
    calibration_data.y_center = STICK_MID_VAL;
    calibration_data.x_min = STICK_MIN_VAL;
    calibration_data.y_min = STICK_MIN_VAL;
    calibration_data.x_max = STICK_MAX_VAL;
    calibration_data.y_max = STICK_MAX_VAL;
    calibration_data.deadband = STICK_DEADBAND;
    calibration_data.expo = STICK_EXPO;
    calibration_data.invert_y = INVERT_Y_AXIS;
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
    if (get_slint_window()) {
      calibration_data.expo = get_slint_window()->global<UiState>().get_expo_value();
    }
    else {
      calibration_data.expo = expo;
    }
  }
  else if (calibration_step == CALIBRATION_STEP_STICK_FLAGS) {
    if (get_slint_window()) {
      calibration_data.invert_y = get_slint_window()->global<UiState>().get_invert_y();
    }
  }
  else if (calibration_step >= CALIBRATION_STEP_DONE) {
    // Save to NVS
    calibration_settings = calibration_data;
    save_input_calibration();

    slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Menu); });
    return;
  }

  calibration_step++;
  reset_min_max_data();
  deadband = STICK_DEADBAND;
  expo = STICK_EXPO;
  update_calibration_ui_strings();
}

extern "C" void handle_input_calibration_secondary() {
  ESP_LOGI(TAG, "Calibration secondary action (Cancel)");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Menu); });
}
