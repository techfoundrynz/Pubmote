#include "imu_driver.h"
#include "esp_log.h"
#include "remote/settings.h"
#if IMU_QMI8658
  #include "imu/qmi8658/imu_driver_qmi8658.hpp"
#endif

static const char *TAG = "PUBREMOTE-IMU";

esp_err_t imu_driver_init() {
#if IMU_QMI8658
  // Initialize the DRV2605 haptic driver
  ESP_LOGI(TAG, "Initializing QMI8568 haptic driver");
  return qmi8658_imu_driver_init();
#elif IMU_BHI260
  return ESP_ERR_NOT_SUPPORTED; // BHI260 not implemented yet
                                // // Initialize the BHI260 haptic driver
                                // ESP_LOGI(TAG, "Initializing BHI260 haptic driver");
                                // return bhi260_imu_driver_init();
#else
  ESP_LOGE(TAG, "No IMU driver defined");
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t imu_driver_deinit() {
#if IMU_QMI8658
  qmi8658_imu_driver_deinit();
  return ESP_OK;
#endif
  return ESP_ERR_NOT_SUPPORTED;
}

void imu_driver_get_data(imu_data_t *data) {
#if IMU_QMI8658
  qmi8658_get_data(data);
#elif IMU_BHI260
  // bhi260_get_data(data);
#endif

  // Apply calibration offsets
  data->accel_x -= imu_calibration.accel_x_offset;
  data->accel_y -= imu_calibration.accel_y_offset;
  data->accel_z -= imu_calibration.accel_z_offset;

  float ax = data->accel_x;
  float ay = data->accel_y;
  float gx = data->gyro_x;
  float gy = data->gyro_y;

  if (imu_calibration.swap_xy) {
    ax = data->accel_y;
    ay = data->accel_x;
    gx = data->gyro_y;
    gy = data->gyro_x;
  }

  if (imu_calibration.invert_x) {
    ax = -ax;
    gx = -gx;
  }
  if (imu_calibration.invert_y) {
    ay = -ay;
    gy = -gy;
  }

  data->accel_x = ax;
  data->accel_y = ay;
  data->gyro_x = gx;
  data->gyro_y = gy;
}

bool imu_driver_is_initialized() {
#if IMU_QMI8658
  extern bool qmi8658_is_active();
  return qmi8658_is_active();
#else
  return false;
#endif
}
