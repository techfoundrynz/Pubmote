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
    mock_speed += 0.2f;
    if (mock_speed > 40.0f) {
      mock_speed = 0.0f;
    }
    RemoteStats reading = stats_snapshot();
    reading.speed = mock_speed;
    reading.dutyCycle = (uint8_t)(mock_speed * 2);
    reading.batteryPercentage = 80;
    reading.batteryVoltage = 74.0f;
    reading.switchState = SWITCH_STATE_BOTH;
    reading.motorTemp = 40.0f;
    reading.controllerTemp = 35.0f;
    reading.tripDistance += 10.0f;
    reading.remoteBatteryPercentage = 95;

    // Mock signal strength (RSSI) so RSSI arcs render correctly
    reading.signalStrength = -55; // RSSI_GOOD is -75, so -55 shows 3 bars

    // Mock battery charging state, cycling every 10 seconds (200 ticks of 50ms)
    static uint32_t tick_count = 0;
    tick_count++;
    if ((tick_count / 200) % 2 == 0) {
      reading.chargeState = CHARGE_STATE_CHARGING;
    }
    else {
      reading.chargeState = CHARGE_STATE_NOT_CHARGING;
    }

    reading.lastUpdated = get_current_time_ms();

    BoardTelemetry board = {.speed = reading.speed,
                            .dutyCycle = reading.dutyCycle,
                            .batteryVoltage = reading.batteryVoltage,
                            .batteryPercentage = reading.batteryPercentage,
                            .tripDistance = reading.tripDistance,
                            .motorTemp = reading.motorTemp,
                            .controllerTemp = reading.controllerTemp,
                            .state = reading.state,
                            .switchState = reading.switchState};
    stats_publish_board(&board, reading.lastUpdated);
    stats_publish_power(reading.remoteBatteryVoltage, reading.remoteBatteryPercentage, reading.chargeState,
                        reading.chargeCurrent);
    stats_set_signal_strength(reading.signalStrength);
    stats_update();

    vTaskDelay(pdMS_TO_TICKS(15));
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
