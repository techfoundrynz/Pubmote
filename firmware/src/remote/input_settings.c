#include "input_settings.h"
#include "config.h"
#include "remote/adc.h"
#include "remote/settings_snapshot.h"
#include "remote/settings_store.h"
#include "remoteinputs.h"
#include "settings_api.h"
#include <string.h>

// One blob, so a pin remap and its calibration reset are committed together.
typedef struct {
  uint32_t version;
  InputPinSettings pins;
  CalibrationSettings calibration;
  uint32_t complete;
} InputSettingsRecord;
_Static_assert(sizeof(InputSettingsRecord) == 32, "Change the input record version when its layout changes");

void settings_reset_calibration(CalibrationSettings *calibration, bool reset_x, bool reset_y) {
  if (reset_x) {
    calibration->x_min = STICK_MIN_VAL;
    calibration->x_max = STICK_MAX_VAL;
    calibration->x_center = STICK_MID_VAL;
  }
  if (reset_y) {
    calibration->y_min = STICK_MIN_VAL;
    calibration->y_max = STICK_MAX_VAL;
    calibration->y_center = STICK_MID_VAL;
  }
  if (reset_x || reset_y) {
    calibration->deadband = STICK_DEADBAND;
  }
}

// An enabled axis still on the built-in range has never been calibrated.
bool settings_calibration_needed(const InputPinSettings *pins, const CalibrationSettings *calibration) {
  bool x_default = calibration->x_min == STICK_MIN_VAL && calibration->x_max == STICK_MAX_VAL &&
                   calibration->x_center == STICK_MID_VAL;
  bool y_default = calibration->y_min == STICK_MIN_VAL && calibration->y_max == STICK_MAX_VAL &&
                   calibration->y_center == STICK_MID_VAL;
  return (pins->js_x_gpio > INPUT_PIN_DISABLED && x_default) || (pins->js_y_gpio > INPUT_PIN_DISABLED && y_default);
}

int settings_store_input_state(const InputPinSettings *pins, const CalibrationSettings *calibration) {
  if (!pins || !calibration) {
    return ESP_ERR_INVALID_ARG;
  }
  InputSettingsRecord record = {0};
  record.version = 1;
  record.pins = *pins;
  record.calibration = *calibration;
  record.complete = 0x49504e31;
  return nvs_write_blob("input_state", &record, sizeof(record));
}

int settings_load_input_state(InputPinSettings *pins, CalibrationSettings *calibration) {
  InputSettingsRecord record = {0};
  int result = nvs_read_blob("input_state", &record, sizeof(record));
  if (result != ESP_OK) {
    return result;
  }
  if (record.version != 1 || record.complete != 0x49504e31) {
    return ESP_ERR_INVALID_ARG;
  }
  *pins = record.pins;
  *calibration = record.calibration;
  return ESP_OK;
}

int settings_save_input_pins(const InputPinSettings *pins) {
  char error[128];
  if (!pins || input_pins_validate(pins, error, sizeof(error)) != ESP_OK) {
    return ESP_ERR_INVALID_ARG;
  }
  const SettingsSnapshot snapshot = settings_snapshot();
  CalibrationSettings pending = snapshot.calibration;
  settings_reset_calibration(&pending, pins->js_x_gpio != snapshot.pins.js_x_gpio,
                             pins->js_y_gpio != snapshot.pins.js_y_gpio);
  return settings_store_input_state(pins, &pending);
}
