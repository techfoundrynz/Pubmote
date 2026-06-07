#include "config.h"

#if TEST_MODE

  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "remote/connection.h"
  #include "remote/stats.h"
  #include "remote/test_mode.h"
  #include "remote/time.h"

static void test_mode_task(void *pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(5000));
  // force connected state
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
    remoteStats.lastUpdated = get_current_time_ms();

    stats_update();

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void test_mode_init(void) {
  xTaskCreate(test_mode_task, "test_mode_task", 8192, NULL, 5, NULL);
}

#endif
