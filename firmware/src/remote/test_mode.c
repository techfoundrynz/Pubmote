#include "config.h"

#if TEST_MODE

  #include "esp_log.h"
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "remote/connection.h"
  #include "remote/stats.h"
  #include "remote/test_mode.h"
  #include "remote/time.h"

static const char *TAG = "PUBREMOTE-TEST_MODE";

static void test_mode_task(void *pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(5000));

  connection_update_state(CONNECTION_STATE_CONNECTED);

  float mock_speed = 0.0f;
  uint32_t loop_counter = 0;

  while (1) {
    mock_speed += 0.5f;
    if (mock_speed > 40.0f) {
      mock_speed = 0.0f;
    }
    remoteStats.speed = mock_speed;
    remoteStats.dutyCycle = (uint8_t)(mock_speed * 2);
    remoteStats.batteryPercentage = 80;
    remoteStats.batteryVoltage = 74.0f;
    remoteStats.switchState = SWITCH_STATE_BOTH;
    remoteStats.motorTemp = 40.0f;
    remoteStats.controllerTemp = 35.0f;
    remoteStats.tripDistance += 10.0f;
    remoteStats.remoteBatteryPercentage = 95;
    remoteStats.lastUpdated = get_current_time_ms();

    stats_update();

    loop_counter++;

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void test_mode_init(void) {
  ESP_LOGI(TAG, "test_mode_init called, spawning test_mode_task");
  BaseType_t ret = xTaskCreate(test_mode_task, "test_mode_task", 4096, NULL, 20, NULL);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "FAILED to create test_mode_task, error code: %d", ret);
  }
  else {
    ESP_LOGI(TAG, "test_mode_task created successfully");
  }
}

#endif
