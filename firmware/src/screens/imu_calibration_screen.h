#pragma once
#include "ui/screen_status.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  void setup_imu_calibration_properties();
  void handle_open_imu_calibration();
  void handle_imu_calibration_back();
  void handle_imu_calibration_primary();

#ifdef __cplusplus
}
#endif
