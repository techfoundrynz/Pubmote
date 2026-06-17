#include "imu.h"
#include "buzzer.h"
#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "imu/imu_datatypes.h"
#include "imu/imu_driver.h"
#include "nvs_flash.h"
#include "settings.h"
#include <driver/gpio.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <nvs.h>
#include "display.h"
#include "powermanagement.h"
#include <math.h>

static int motionless_counter = 0;

static const char *TAG = "PUBREMOTE-IMU";
static TaskHandle_t imu_task_handle = NULL;
static QueueHandle_t imu_event_queue = NULL;

#define DEBUG_IMU 0

static void IRAM_ATTR imu_isr_handler(void *arg) {
  uint32_t gpio_num = (uint32_t)arg;
  xQueueSendFromISR(imu_event_queue, &gpio_num, NULL);
}

static void imu_get_data() {
#if IMU_ENABLED
  imu_data_t imu_data = {0};
  imu_driver_get_data(&imu_data);

  #if DEBUG_IMU
  ESP_LOGI(TAG, "IMU Data - Accel: [%.2f, %.2f, %.2f], Gyro: [%.2f, %.2f, %.2f], Event: %d", imu_data.accel_x,
           imu_data.accel_y, imu_data.accel_z, imu_data.gyro_x, imu_data.gyro_y, imu_data.gyro_z, imu_data.event);
  #endif

  // Gesture Detection: Raise wrist / Lift remote to HBM
  if (device_settings.raise_to_hbm && display_supports_hbm() && !is_pocket_mode_enabled()) {
    // Reconstruct raw Z acceleration (adds back the calibrated level offset)
    float raw_z = imu_data.accel_z + imu_calibration.accel_z_offset;

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

    if (is_flat && is_motionless) {
      if (motionless_counter < 100) { // cap at 5 seconds (50ms * 100)
        motionless_counter++;
      }
    } else {
      motionless_counter = 0;
    }

    if (is_flat) {
      // If the remote is flat but has been completely motionless for more than 3 seconds (60 samples at 20Hz),
      // we assume it is lying flat on a table/desk, so we turn HBM off to save battery.
      // Otherwise, if it's being actively held or just raised, we turn HBM on.
      if (motionless_counter > 60) {
        if (display_get_hbm()) {
          ESP_LOGI(TAG, "Table detection: flat & motionless. Disabling HBM.");
          display_set_hbm(false);
        }
      } else {
        if (!display_get_hbm()) {
          ESP_LOGI(TAG, "Raise-to-HBM: viewing position detected. Enabling HBM.");
          display_set_hbm(true);
        }
        // Always reset sleep timer while actively holding it at viewing angle to prevent sleeping
        reset_sleep_timer();
      }
    } else {
      // Tilt is outside the viewing window (pointing down, hanging, or turned away)
      if (display_get_hbm()) {
        ESP_LOGI(TAG, "Wrist/remote lowered. Disabling HBM.");
        display_set_hbm(false);
      }
    }
  }
#endif
}

void imu_task(void *pvParameters) {

#ifdef IMU_INT

#endif

  while (1) {
#ifdef IMU_INT
    uint32_t io_num;
    while (xQueueReceive(imu_event_queue, &io_num, 0) == pdTRUE) {
      imu_get_data();
    }
    vTaskDelay(pdMS_TO_TICKS(50));
#else
    imu_get_data();
    vTaskDelay(pdMS_TO_TICKS(50));
#endif
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

  #ifdef IMU_INT
  gpio_config_t imu_io_conf = {};
  imu_io_conf.intr_type = GPIO_INTR_ANYEDGE;
  imu_io_conf.mode = GPIO_MODE_INPUT;
  imu_io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  imu_io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  imu_io_conf.pin_bit_mask = BIT64(IMU_INT);
  gpio_config(&imu_io_conf);
  imu_event_queue = xQueueCreate(1, sizeof(uint32_t));

  gpio_isr_handler_add(IMU_INT, imu_isr_handler, (void *)IMU_INT);
  #endif

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
  #ifdef IMU_INT
  gpio_isr_handler_remove(IMU_INT);
  #endif
#endif
}