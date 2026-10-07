#pragma once
#include "esp_err.h"
#include "imu/imu_driver.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef enum {
    IMU_GESTURE_LOWERED,
    IMU_GESTURE_RAISED,
    IMU_GESTURE_TABLE_FLAT,
    IMU_GESTURE_DOUBLE_TAP,
  } imu_gesture_t;

  typedef void (*imu_gesture_cb_t)(imu_gesture_t gesture);

  void imu_init();
  void imu_deinit();
  // Raises the IMU task to 100 Hz while an animated screen needs motion input.
  void imu_set_motion_sampling(bool enabled);
  // Mean of the calibrated samples since the previous call (the latest sample if
  // none arrived since). Never reads I2C. False when no sample is under 250 ms old.
  bool imu_take_motion_sample(imu_data_t *mean);
  esp_err_t imu_register_gesture_callback(imu_gesture_cb_t cb);
  esp_err_t imu_unregister_gesture_callback(imu_gesture_cb_t cb);

#ifdef __cplusplus
}
#endif
