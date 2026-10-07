// Exercise the production adapter with only the sensor acquisition mocked.
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#define IMU_QMI8658 1
#include "remote/settings_types.h"
ImuCalibrationSettings imu_calibration;
#include "imu/imu_driver.c"

static bool acquisition_ok;
static int acquisitions;
static const imu_data_t raw = {1.5f, -2.0f, 3.25f, 10.0f, -20.0f, 30.0f, IMU_EVENT_TAP};

static void assert_axes(const imu_data_t *data, float ax, float ay, float az, float gx, float gy, float gz) {
  assert(data->accel_x == ax && data->accel_y == ay && data->accel_z == az);
  assert(data->gyro_x == gx && data->gyro_y == gy && data->gyro_z == gz);
}

bool qmi8658_get_data(imu_data_t *data) {
  ++acquisitions;
  assert(data != NULL);
  // An unavailable device or failed transfer publishes no axes. The adapter
  // must discard the caller's old sample before attempting a new acquisition.
  assert_axes(data, 0, 0, 0, 0, 0, 0);
  if (!acquisition_ok) return false;
  *data = raw;
  return true;
}
esp_err_t qmi8658_imu_driver_init(void) { return ESP_OK; }
void qmi8658_imu_driver_deinit(void) {}
bool qmi8658_is_active(void) { return true; }

int main(void) {
  imu_data_t sample = raw;
  imu_calibration = (ImuCalibrationSettings){0.25f, -0.5f, 1.25f, false, false, false, false};
  acquisition_ok = true;
  assert(imu_driver_get_data(&sample));
  assert_axes(&sample, 1.25f, -1.5f, 2.0f, 10, -20, 30);
  assert(sample.event == IMU_EVENT_TAP);

  // The deployed Pingumote mapping swaps X/Y and reverses screen-normal Z.
  imu_calibration.swap_xy = true;
  imu_calibration.invert_z = true;
  assert(imu_driver_get_data(&sample));
  assert_axes(&sample, -1.5f, 1.25f, -2.0f, -20, 10, -30);

  // Reversal acts on the mapped axes; offsets remain in physical sensor axes.
  imu_calibration.invert_x = true;
  imu_calibration.invert_y = true;
  assert(imu_driver_get_data(&sample));
  assert_axes(&sample, 1.5f, -1.25f, -2.0f, 20, -10, -30);

  // A failed read must never turn nonzero calibration offsets into a fake
  // acceleration sample or leave the previous successful sample available.
  acquisition_ok = false;
  assert(!imu_driver_get_data(&sample));
  assert_axes(&sample, 0, 0, 0, 0, 0, 0);
  assert(acquisitions == 4);
  assert(!imu_driver_get_data(NULL));
  assert(acquisitions == 4);

  acquisition_ok = true;
  assert(imu_driver_get_data(&sample));
  assert_axes(&sample, 1.5f, -1.25f, -2.0f, 20, -10, -30);
  puts("IMU adapter: calibrated axis mapping, failed acquisition, null output and recovery passed.");
  return 0;
}
