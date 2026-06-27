#include "screens/imu_calibration_screen.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "remote/display.h"
#include "generated/app-window.h"
#include "imu/imu_driver.h"
#include "remote/settings.h"
#include <stdio.h>

static const char *TAG = "PUBREMOTE-IMU_CALIBRATION_SCREEN";
static TaskHandle_t imu_calibration_task_handle = NULL;
static ImuCalibrationSettings original_imu_calibration;
static int imu_calibration_step = 1;

static void update_imu_calibration_ui_strings() {
  slint::invoke_from_event_loop([]() {
    if (get_slint_window()) {
      const auto &state = get_slint_window()->global<UiState>();
      
      switch (imu_calibration_step) {
        case 1:
          state.set_imu_calibration_step_text("Verify the bubble level moves. Tap Next to begin calibration.");
          state.set_imu_calibration_primary_button_text("Next");
          state.set_imu_calibration_show_axis_controls(false);
          break;
        case 2:
          state.set_imu_calibration_step_text("Place the remote on a flat, level surface and tap Calibrate.");
          state.set_imu_calibration_primary_button_text("Calibrate");
          state.set_imu_calibration_show_axis_controls(false);
          break;
        case 3:
          state.set_imu_calibration_step_text("Verify direction. Use Inv X/Y and Swap XY buttons below if needed.");
          state.set_imu_calibration_primary_button_text("Next");
          state.set_imu_calibration_show_axis_controls(true);
          break;
        case 4:
          state.set_imu_calibration_step_text("Confirm the bubble level tilts correctly. Tap Save to apply.");
          state.set_imu_calibration_primary_button_text("Save");
          state.set_imu_calibration_show_axis_controls(false);
          break;
        default:
          break;
      }
    }
  });
}

static const char* event_to_string(imu_event_t event) {
  switch (event) {
    case IMU_EVENT_NONE:
      return "None";
    case IMU_EVENT_WOM_MOTION:
      return "Motion";
    case IMU_EVENT_TAP:
      return "Tap";
    case IMU_EVENT_ORIENTATION_CHANGE:
      return "Orientation Change";
    default:
      return "Unknown";
  }
}

static void imu_calibration_task(void *pvParameters) {
  ESP_LOGI(TAG, "IMU screen task started");
  imu_data_t data = {};
  int log_counter = 0;

  while (is_imu_calibration_screen_active()) {
#if IMU_ENABLED
    imu_driver_get_data(&data);

    char accel_str[64];
    char gyro_str[64];
    char gesture_str[64];
    snprintf(accel_str, sizeof(accel_str), "A: X=%.2f Y=%.2f Z=%.2f", data.accel_x, data.accel_y, data.accel_z);
    snprintf(gyro_str, sizeof(gyro_str), "G: X=%.1f Y=%.1f Z=%.1f", data.gyro_x, data.gyro_y, data.gyro_z);
    
    // Use calibrated/inverted Z directly to evaluate viewing tilt angle and orientation status
    float raw_z = data.accel_z;
    const char *face_status = "Tilted";
    if (raw_z > 0.70f) {
      face_status = "Face Up";
    } else if (raw_z < -0.70f) {
      face_status = "Face Down";
    }
    snprintf(gesture_str, sizeof(gesture_str), "Orient: %s (Z=%.2f)", face_status, raw_z);

    if (++log_counter >= 20) { // Log at 1Hz (20 * 50ms)
      log_counter = 0;
      ESP_LOGI(TAG, "Live IMU - Accel: [%.2f, %.2f, %.2f], Gyro: [%.1f, %.1f, %.1f], Orient: %s, Event: %s",
               data.accel_x, data.accel_y, data.accel_z, data.gyro_x, data.gyro_y, data.gyro_z, face_status,
               event_to_string(data.event));
    }

    slint::invoke_from_event_loop([=]() {
      if (get_slint_window()) {
        const auto &state = get_slint_window()->global<UiState>();
        state.set_imu_calibration_accel_x(data.accel_x);
        state.set_imu_calibration_accel_y(data.accel_y);
        state.set_imu_calibration_accel_z(data.accel_z);
        state.set_imu_calibration_gyro_x(data.gyro_x);
        state.set_imu_calibration_gyro_y(data.gyro_y);
        state.set_imu_calibration_gyro_z(data.gyro_z);
        state.set_imu_calibration_accel_text(accel_str);
        state.set_imu_calibration_gyro_text(gyro_str);
        state.set_imu_calibration_event_text(gesture_str);
      }
    });
#endif
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  ESP_LOGI(TAG, "IMU calibration task ended");
  imu_calibration_task_handle = NULL;
  vTaskDelete(NULL);
}

extern "C" void setup_imu_calibration_properties() {
  ESP_LOGI(TAG, "setup_imu_calibration_properties called. IMU_ENABLED = %d", IMU_ENABLED);
  if (!get_slint_window()) return;

  original_imu_calibration = imu_calibration;
  imu_calibration_step = 1;
  update_imu_calibration_ui_strings();

#if IMU_ENABLED
  if (!imu_driver_is_initialized()) {
    ESP_LOGI(TAG, "IMU not initialized. Attempting re-initialization...");
    esp_err_t err = imu_driver_init();
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Re-initialization succeeded");
    } else {
      ESP_LOGE(TAG, "Re-initialization failed: %d", err);
    }
  }
#endif

  slint::invoke_from_event_loop([]() {
    if (get_slint_window()) {
      const auto &state = get_slint_window()->global<UiState>();
      state.set_imu_calibration_enabled(IMU_ENABLED && imu_driver_is_initialized());
      state.set_imu_calibration_invert_x(imu_calibration.invert_x);
      state.set_imu_calibration_invert_y(imu_calibration.invert_y);
      state.set_imu_calibration_swap_xy(imu_calibration.swap_xy);

      state.on_imu_calibration_calibrate_level([]() {
        ESP_LOGI(TAG, "Calibrating level offsets...");
        ImuCalibrationSettings temp = imu_calibration;
        imu_calibration.accel_x_offset = 0.0f;
        imu_calibration.accel_y_offset = 0.0f;
        imu_calibration.accel_z_offset = 0.0f;
        imu_calibration.invert_x = false;
        imu_calibration.invert_y = false;
        imu_calibration.invert_z = false;
        imu_calibration.swap_xy = false;
        imu_data_t raw_data = {};
        imu_driver_get_data(&raw_data);
        imu_calibration = temp;
        imu_calibration.accel_x_offset = raw_data.accel_x;
        imu_calibration.accel_y_offset = raw_data.accel_y;
        if (raw_data.accel_z < 0.0f) {
          imu_calibration.invert_z = true;
          imu_calibration.accel_z_offset = raw_data.accel_z + 1.0f;
        } else {
          imu_calibration.invert_z = false;
          imu_calibration.accel_z_offset = raw_data.accel_z - 1.0f;
        }
        ESP_LOGI(TAG, "Level calibrated (in-memory). Offsets: X=%.4f, Y=%.4f, Z=%.4f",
                 imu_calibration.accel_x_offset, imu_calibration.accel_y_offset, imu_calibration.accel_z_offset);
      });

      state.on_imu_calibration_toggle_invert_x([]() {
        imu_calibration.invert_x = !imu_calibration.invert_x;
        ESP_LOGI(TAG, "Invert X toggled (in-memory): %d", imu_calibration.invert_x);
        slint::invoke_from_event_loop([]() {
          if (get_slint_window()) {
            get_slint_window()->global<UiState>().set_imu_calibration_invert_x(imu_calibration.invert_x);
          }
        });
      });

      state.on_imu_calibration_toggle_invert_y([]() {
        imu_calibration.invert_y = !imu_calibration.invert_y;
        ESP_LOGI(TAG, "Invert Y toggled (in-memory): %d", imu_calibration.invert_y);
        slint::invoke_from_event_loop([]() {
          if (get_slint_window()) {
            get_slint_window()->global<UiState>().set_imu_calibration_invert_y(imu_calibration.invert_y);
          }
        });
      });

      state.on_imu_calibration_toggle_swap_xy([]() {
        imu_calibration.swap_xy = !imu_calibration.swap_xy;
        ESP_LOGI(TAG, "Swap X/Y toggled (in-memory): %d", imu_calibration.swap_xy);
        slint::invoke_from_event_loop([]() {
          if (get_slint_window()) {
            get_slint_window()->global<UiState>().set_imu_calibration_swap_xy(imu_calibration.swap_xy);
          }
        });
      });
      }
  });

#if IMU_ENABLED
  if (imu_calibration_task_handle == NULL) {
    xTaskCreate(imu_calibration_task, "imu_calibration_task", 3072, NULL, 2, &imu_calibration_task_handle);
    ESP_LOGI(TAG, "Created imu_calibration_task");
  } else {
    ESP_LOGI(TAG, "imu_calibration_task already running");
  }
#else
  ESP_LOGW(TAG, "IMU is disabled at compile time via IMU_ENABLED");
#endif
}

extern "C" void handle_open_imu_calibration() {
  ESP_LOGI(TAG, "Open IMU calibration pressed");
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::ImuCalibration);
  });
}

extern "C" void handle_imu_calibration_back() {
  ESP_LOGI(TAG, "IMU back/cancel pressed. Restoring original calibration settings...");
  imu_calibration = original_imu_calibration;
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::Menu);
  });
}

extern "C" void handle_imu_calibration_primary() {
  ESP_LOGI(TAG, "IMU calibration primary action at step %d", imu_calibration_step);

  if (imu_calibration_step == 1) {
    imu_calibration_step = 2;
    update_imu_calibration_ui_strings();
  }
  else if (imu_calibration_step == 2) {
    ESP_LOGI(TAG, "Calibrating level offsets...");
    ImuCalibrationSettings temp = imu_calibration;
    imu_calibration.accel_x_offset = 0.0f;
    imu_calibration.accel_y_offset = 0.0f;
    imu_calibration.accel_z_offset = 0.0f;
    imu_calibration.invert_x = false;
    imu_calibration.invert_y = false;
    imu_calibration.invert_z = false;
    imu_calibration.swap_xy = false;
    
    imu_data_t raw_data = {};
    imu_driver_get_data(&raw_data);
    
    imu_calibration = temp;
    imu_calibration.accel_x_offset = raw_data.accel_x;
    imu_calibration.accel_y_offset = raw_data.accel_y;
    if (raw_data.accel_z < 0.0f) {
      imu_calibration.invert_z = true;
      imu_calibration.accel_z_offset = raw_data.accel_z + 1.0f;
    } else {
      imu_calibration.invert_z = false;
      imu_calibration.accel_z_offset = raw_data.accel_z - 1.0f;
    }
    
    ESP_LOGI(TAG, "Level calibrated. Offsets: X=%.4f, Y=%.4f, Z=%.4f",
             imu_calibration.accel_x_offset, imu_calibration.accel_y_offset, imu_calibration.accel_z_offset);
             
    imu_calibration_step = 3;
    update_imu_calibration_ui_strings();
  }
  else if (imu_calibration_step == 3) {
    imu_calibration_step = 4;
    update_imu_calibration_ui_strings();
  }
  else if (imu_calibration_step == 4) {
    ESP_LOGI(TAG, "IMU Calibration completed. Saving to NVS...");
    save_imu_calibration();
    
    slint::invoke_from_event_loop([]() {
      get_slint_window()->global<UiState>().set_screen(Screen::Menu);
    });
  }
}
