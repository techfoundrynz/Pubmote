#include "config.h"

#if TEST_MODE

  #include "esp_log.h"
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "remote/connection.h"
  #include "remote/stats.h"
  #include "remote/test_mode.h"
  #include "remote/time.h"

static const char *TAG = "TEST_MODE";

static void test_mode_task(void *pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(5000));
  connection_update_state(CONNECTION_STATE_CONNECTED);
  float mock_speed = 0.0f;

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

    // Mock signal strength (RSSI) so RSSI arcs render correctly
    remoteStats.signalStrength = -55; // RSSI_GOOD is -75, so -55 shows 3 bars

    // Mock battery charging state, cycling every 10 seconds (200 ticks of 50ms)
    static uint32_t tick_count = 0;
    tick_count++;
    if ((tick_count / 200) % 2 == 0) {
      remoteStats.chargeState = CHARGE_STATE_CHARGING;
    }
    else {
      remoteStats.chargeState = CHARGE_STATE_NOT_CHARGING;
    }

    remoteStats.lastUpdated = get_current_time_ms();

    stats_update();

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void test_mode_init(void) {
  ESP_LOGI(TAG, "Initializing test mode...");
  BaseType_t ret = xTaskCreate(test_mode_task, "test_mode_task", 4096, NULL, 5, NULL);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create test_mode_task! error: %d", (int)ret);
  }
  else {
    ESP_LOGI(TAG, "test_mode_task created successfully");
  }
}

#endif
