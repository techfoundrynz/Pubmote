#include "imu.h"
#include "buzzer.h"
#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "imu/imu_datatypes.h"
#include "imu/imu_driver.h"
#include "settings.h"
#include "utilities/psram_task.h"
#include <driver/gpio.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <math.h>

static int motionless_counter = 0;

static const char *TAG = "PUBREMOTE-IMU";
static TaskHandle_t imu_task_handle = NULL;
static volatile bool imu_should_run = false;
static volatile bool imu_running = false;

// Motion input for animated screens. While enabled the task samples at 100 Hz
// and accumulates a mean, so a consumer polling at its frame rate sees every
// jolt instead of whichever single sample happened to be newest.
static portMUX_TYPE sample_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool motion_sampling = false;
static imu_data_t latest_sample;
static int64_t latest_sample_us = 0;
static imu_data_t sample_sum;
static int sample_count = 0;

void imu_set_motion_sampling(bool enabled) {
  portENTER_CRITICAL(&sample_lock);
  motion_sampling = enabled;
  sample_count = 0;
  portEXIT_CRITICAL(&sample_lock);
}

bool imu_take_motion_sample(imu_data_t *mean) {
  if (!mean) {
    return false;
  }
  portENTER_CRITICAL(&sample_lock);
  const int count = sample_count;
  const int64_t sample_us = latest_sample_us;
  *mean = count > 0 ? sample_sum : latest_sample;
  sample_count = 0;
  portEXIT_CRITICAL(&sample_lock);
  if (count > 1) {
    mean->accel_x /= count;
    mean->accel_y /= count;
    mean->accel_z /= count;
    mean->gyro_x /= count;
    mean->gyro_y /= count;
    mean->gyro_z /= count;
  }
  const int64_t age = esp_timer_get_time() - sample_us;
  return sample_us > 0 && age >= 0 && age <= 250000;
}

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

static void imu_get_data(bool run_gestures) {
#if IMU_ENABLED
  imu_data_t imu_data = {0};
  const bool sample_ok = imu_driver_get_data(&imu_data);

  // Failed transfers never refresh the cache, even with calibration offsets.
  const float norm2 =
      imu_data.accel_x * imu_data.accel_x + imu_data.accel_y * imu_data.accel_y + imu_data.accel_z * imu_data.accel_z;
  if (sample_ok && isfinite(norm2) && norm2 > 0.01f && norm2 < 100.0f && isfinite(imu_data.gyro_x) &&
      isfinite(imu_data.gyro_y) && isfinite(imu_data.gyro_z)) {
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&sample_lock);
    latest_sample = imu_data;
    latest_sample_us = now;
    if (sample_count == 0) {
      sample_sum = imu_data;
    }
    else {
      sample_sum.accel_x += imu_data.accel_x;
      sample_sum.accel_y += imu_data.accel_y;
      sample_sum.accel_z += imu_data.accel_z;
      sample_sum.gyro_x += imu_data.gyro_x;
      sample_sum.gyro_y += imu_data.gyro_y;
      sample_sum.gyro_z += imu_data.gyro_z;
    }
    if (sample_count < 1000) {
      sample_count++;
    }
    portEXIT_CRITICAL(&sample_lock);
  }

  if (imu_data.event == IMU_EVENT_DOUBLE_TAP) {
    ESP_LOGI(TAG, "Double tap event detected! Triggering callback.");
    trigger_gesture_callbacks(IMU_GESTURE_DOUBLE_TAP);
  }

  // Gesture detection is tuned for a 50 ms cadence.
  if (!run_gestures) {
    return;
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
  float gyro_mag =
      sqrtf(imu_data.gyro_x * imu_data.gyro_x + imu_data.gyro_y * imu_data.gyro_y + imu_data.gyro_z * imu_data.gyro_z);

  // Z acceleration component is close to 1g (screen facing up/towards user).
  // We allow a comfortable tilt range (Z > 0.70g corresponds to tilt < ~45 degrees).
  bool is_flat = (raw_z > 0.70f);

  // Gyroscope angular velocity is low (< 8 dps), indicating the device is sitting stable
  // on a table, desk, or resting flat.
  bool is_motionless = (gyro_mag < 8.0f);

  if (should_log) {
    ESP_LOGI(TAG,
             "Gesture check - Raw Z: %.3f (Accel Z: %.3f, Offset: %.3f), Gyro Mag: %.3f, Flat: %d, Motionless: %d, "
             "Counter: %d",
             raw_z, imu_data.accel_z, imu_calibration.accel_z_offset, gyro_mag, is_flat, is_motionless,
             motionless_counter);
  }

  if (is_flat && is_motionless) {
    if (motionless_counter < 100) { // cap at 5 seconds (50ms * 100)
      motionless_counter++;
    }
  }
  else {
    motionless_counter = 0;
  }

  imu_gesture_t current_gesture = IMU_GESTURE_LOWERED;
  if (is_flat) {
    if (motionless_counter > 60) {
      current_gesture = IMU_GESTURE_TABLE_FLAT;
    }
    else {
      current_gesture = IMU_GESTURE_RAISED;
    }
  }

  trigger_gesture_callbacks(current_gesture);
#endif
}

void imu_task(void *pvParameters) {
  imu_running = true;
  int tick = 0;
  while (imu_should_run) {
    // 10 ms ticks while motion sampling, else 50 ms. Gestures stay at 50 ms.
    const bool fast = motion_sampling;
    imu_get_data(!fast || tick % 5 == 0);
    tick = fast ? tick + 1 : 0;
    vTaskDelay(pdMS_TO_TICKS(fast ? 10 : 50));
  }

  ESP_LOGI(TAG, "IMU task ended");
  imu_running = false;
  // terminate self
  vTaskDelete(NULL);
}

static StaticTask_t imu_task_tcb;
static StackType_t *imu_task_stack;

void imu_init() {
  ESP_LOGI(TAG, "imu_init called. IMU_ENABLED = %d", IMU_ENABLED);
#if IMU_ENABLED
  esp_err_t err = imu_driver_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "imu_driver_init failed with error: %d", err);
  }
  else {
    ESP_LOGI(TAG, "imu_driver_init succeeded");
  }

  imu_should_run = true;
  imu_task_handle = create_psram_task(imu_task, "imu_task", 4096, NULL, 2, &imu_task_tcb, &imu_task_stack);
#endif
}

void imu_deinit() {
#if IMU_ENABLED
  if (imu_task_handle != NULL) {
    // Ask it to exit and wait. vTaskDelete does not release mutexes the victim holds, and this
    // task spends its life in I2C transactions - killing it mid-transfer strands the driver's
    // bus mutex and every later transfer on the bus times out for good.
    imu_should_run = false;
    for (int i = 0; i < 100 && imu_running; i++) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (imu_running) {
      ESP_LOGE(TAG, "IMU task did not exit - forcing deletion");
      vTaskDelete(imu_task_handle);
      imu_running = false;
    }

    imu_task_handle = NULL;
  }
  imu_driver_deinit();
#endif
  portENTER_CRITICAL(&sample_lock);
  latest_sample_us = 0;
  sample_count = 0;
  portEXIT_CRITICAL(&sample_lock);
}
