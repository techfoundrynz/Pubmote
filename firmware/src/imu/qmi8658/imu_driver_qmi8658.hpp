#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "imu/imu_datatypes.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t qmi8658_imu_driver_init();
void qmi8658_imu_driver_deinit();
bool qmi8658_is_active();
void qmi8658_get_data(imu_data_t *data);

#ifdef __cplusplus
}
#endif