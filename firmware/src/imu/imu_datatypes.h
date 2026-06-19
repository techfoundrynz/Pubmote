#ifndef __IMU_DATA_H
#define __IMU_DATA_H

typedef enum {
  IMU_EVENT_NONE,
  IMU_EVENT_WOM_MOTION,
  IMU_EVENT_TAP,
  IMU_EVENT_DOUBLE_TAP,
  IMU_EVENT_ORIENTATION_CHANGE
} imu_event_t;

// Define IMU data structure
typedef struct {
  float accel_x;
  float accel_y;
  float accel_z;
  float gyro_x;
  float gyro_y;
  float gyro_z;
  imu_event_t event;
} imu_data_t;

#endif