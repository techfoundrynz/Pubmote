#include "imu.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu/imu_driver.h"
#include "imu/raise_detector.h"
#include "utilities/psram_task.h"

#if IMU_ENABLED
static raise_detector_t raise_detector;
#endif

static const char *TAG = "PUBREMOTE-IMU";
static TaskHandle_t imu_task_handle = NULL;
static volatile bool imu_should_run = false;
static volatile bool imu_running = false;

#ifndef DEBUG_IMU
  #define DEBUG_IMU 0
#endif

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

  uint64_t now_ms = esp_timer_get_time() / 1000;
  bool raised = raise_detector_update(&raise_detector, now_ms, imu_data.accel_x, imu_data.accel_y, imu_data.accel_z,
                                      imu_data.accel_valid);
  #if DEBUG_IMU
  // CSV at the detector's sample rate for replay; gyro is diagnostic only.
  ESP_LOGI(TAG, "raise_trace,%llu,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%d,%d,%d", (unsigned long long)now_ms, imu_data.accel_x,
           imu_data.accel_y, imu_data.accel_z, imu_data.gyro_x, imu_data.gyro_y, imu_data.gyro_z, imu_data.accel_valid,
           imu_data.gyro_valid, raised);
  #endif
  imu_gesture_t current_gesture = raised ? IMU_GESTURE_RAISED : IMU_GESTURE_LOWERED;
  trigger_gesture_callbacks(current_gesture);
#endif
}

void imu_task(void *pvParameters) {
  imu_running = true;
  while (imu_should_run) {
    imu_get_data();
    vTaskDelay(pdMS_TO_TICKS(50));
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

  raise_detector_reset(&raise_detector);
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
}
