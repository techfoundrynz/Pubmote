#ifndef __IMU_CALIBRATION_SCREEN_H
#define __IMU_CALIBRATION_SCREEN_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool is_imu_calibration_screen_active();
void setup_imu_calibration_properties();
void handle_open_imu_calibration();
void handle_imu_calibration_back();
void handle_imu_calibration_primary();

#ifdef __cplusplus
}
#endif

#endif
