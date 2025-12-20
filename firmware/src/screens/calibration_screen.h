#ifndef __CALIBRATION_SCREEN_H
#define __CALIBRATION_SCREEN_H

typedef enum {
  CALIBRATION_STEP_START,
  CALIBRATION_STEP_CENTER,
  CALIBRATION_STEP_MINMAX,
  CALIBRATION_STEP_DEADBAND,
  CALIBRATION_STEP_EXPO,
  CALIBRATION_STEP_STICK_FLAGS,
  CALIBRATION_STEP_DONE
} CalibrationStep;

bool is_calibration_screen_active();

void calibration_screen_load_start(lv_event_t *e);
void calibration_screen_loaded(lv_event_t *e);
void calibration_screen_unload_start(lv_event_t *e);
void calibration_settings_primary_button_press(lv_event_t *e);
void calibration_settings_secondary_button_press(lv_event_t *e);
void expo_slider_change(lv_event_t *e);

#endif