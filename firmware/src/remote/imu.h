#ifndef __IMU_H
#define __IMU_H
#include "imu/imu_driver.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
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
esp_err_t imu_register_gesture_callback(imu_gesture_cb_t cb);
esp_err_t imu_unregister_gesture_callback(imu_gesture_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif