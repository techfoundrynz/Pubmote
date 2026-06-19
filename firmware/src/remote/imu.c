#include "imu.h"
#include "buzzer.h"
#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "imu/imu_datatypes.h"
#include "imu/imu_driver.h"
#include "nvs_flash.h"
#include "settings.h"
#include <driver/gpio.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <nvs.h>
#include <math.h>

static int motionless_counter = 0;

static const char *TAG = "PUBREMOTE-IMU";
static TaskHandle_t imu_task_handle = NULL;

#define DEBUG_IMU 0

#if IMU_ENABLED
#define MAX_IMU_CALLBACKS 4
static imu_gesture_cb_t gesture_callbacks[MAX_IMU_CALLBACKS] = {NULL};

esp_err_t imu_register_gesture_callback(imu_gesture_cb_t cb) {
  for (int i = 0; i < MAX_IMU_CALLBACKS; i++) {
    if (gesture_callbacks[i] == cb) {
      return ESP_OK; // already registered
    }
    if (gesture_callbacks[i] == NULL) {
      gesture_callbacks[i] = cb;
      return ESP_OK;
    }
  }
  return ESP_ERR_NO_MEM;
}

esp_err_t imu_unregister_gesture_callback(imu_gesture_cb_t cb) {
  for (int i = 0; i < MAX_IMU_CALLBACKS; i++) {
    if (gesture_callbacks[i] == cb) {
      gesture_callbacks[i] = NULL;
      return ESP_OK;
    }
  }
  return ESP_ERR_NOT_FOUND;
}

static void trigger_gesture_callbacks(imu_gesture_t gesture) {
  for (int i = 0; i < MAX_IMU_CALLBACKS; i++) {
    if (gesture_callbacks[i] != NULL) {
      gesture_callbacks[i](gesture);
    }
  }
}
#else
esp_err_t imu_register_gesture_callback(imu_gesture_cb_t cb) {
  return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t imu_unregister_gesture_callback(imu_gesture_cb_t cb) {
  return ESP_ERR_NOT_SUPPORTED;
}
#endif

static void imu_get_data() {
#if IMU_ENABLED
  imu_data_t imu_data = {0};
  imu_driver_get_data(&imu_data);

  if (imu_data.event == IMU_EVENT_DOUBLE_TAP) {
    ESP_LOGI(TAG, "Double tap event detected! Triggering callback.");
    trigger_gesture_callbacks(IMU_GESTURE_DOUBLE_TAP);
  }

  #if DEBUG_IMU
  ESP_LOGI(TAG, "IMU Data - Accel: [%.2f, %.2f, %.2f], Gyro: [%.2f, %.2f, %.2f], Event: %d", imu_data.accel_x,
           imu_data.accel_y, imu_data.accel_z, imu_data.gyro_x, imu_data.gyro_y, imu_data.gyro_z, imu_data.event);
  #endif

  static uint64_t last_log_time_ms = 0;
  uint64_t now_ms = esp_timer_get_time() / 1000;
  bool should_log = (now_ms - last_log_time_ms >= 1000);
  if (should_log) {
    last_log_time_ms = now_ms;
  }

  // Use calibrated Z acceleration directly
  float raw_z = imu_data.accel_z;

  // Calculate gyroscope magnitude (angular velocity in dps)
  float gyro_mag = sqrtf(imu_data.gyro_x * imu_data.gyro_x +
                         imu_data.gyro_y * imu_data.gyro_y +
                         imu_data.gyro_z * imu_data.gyro_z);

  // Z acceleration component is close to 1g (screen facing up/towards user).
  // We allow a comfortable tilt range (Z > 0.70g corresponds to tilt < ~45 degrees).
  bool is_flat = (raw_z > 0.70f);

  // Gyroscope angular velocity is low (< 8 dps), indicating the device is sitting stable
  // on a table, desk, or resting flat.
  bool is_motionless = (gyro_mag < 8.0f);

  if (should_log) {
    ESP_LOGI(TAG, "Gesture check - Raw Z: %.3f (Accel Z: %.3f, Offset: %.3f), Gyro Mag: %.3f, Flat: %d, Motionless: %d, Counter: %d",
             raw_z, imu_data.accel_z, imu_calibration.accel_z_offset, gyro_mag, is_flat, is_motionless, motionless_counter);
  }

  if (is_flat && is_motionless) {
    if (motionless_counter < 100) { // cap at 5 seconds (50ms * 100)
      motionless_counter++;
    }
  } else {
    motionless_counter = 0;
  }

  imu_gesture_t current_gesture = IMU_GESTURE_LOWERED;
  if (is_flat) {
    if (motionless_counter > 60) {
      current_gesture = IMU_GESTURE_TABLE_FLAT;
    } else {
      current_gesture = IMU_GESTURE_RAISED;
    }
  }

  trigger_gesture_callbacks(current_gesture);
#endif
}

void imu_task(void *pvParameters) {
  while (1) {
    imu_get_data();
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  ESP_LOGI(TAG, "IMU task ended");
  // terminate self
  vTaskDelete(NULL);
}

void imu_init() {
  ESP_LOGI(TAG, "imu_init called. IMU_ENABLED = %d", IMU_ENABLED);
#if IMU_ENABLED
  esp_err_t err = imu_driver_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "imu_driver_init failed with error: %d", err);
  } else {
    ESP_LOGI(TAG, "imu_driver_init succeeded");
  }

  xTaskCreate(imu_task, "imu_task", 4096, NULL, 2, &imu_task_handle);
#endif
}

void imu_deinit() {
#if IMU_ENABLED
  imu_driver_deinit();
  if (imu_task_handle != NULL) {
    vTaskDelete(imu_task_handle);
    imu_task_handle = NULL;
  }
#endif
}