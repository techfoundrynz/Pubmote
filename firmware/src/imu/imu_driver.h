#ifndef __IMU_DRIVER_H
#define __IMU_DRIVER_H
#include "imu/imu_datatypes.h"
#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t imu_driver_init();
esp_err_t imu_driver_deinit();
void imu_driver_get_data(imu_data_t *data);
bool imu_driver_is_initialized();

#ifdef __cplusplus
}
#endif

#endif