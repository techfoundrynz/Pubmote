#include "screens/charge_screen.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "remote/display.h"
#include "remote/stats.h"
#include "generated/app-window.h"

static const char *TAG = "PUBREMOTE-CHARGE_SCREEN";
static TaskHandle_t charge_task_handle = NULL;

void update_charge_percentage() {
  if (!get_slint_window()) return;
  
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_charge_percent(remoteStats.remoteBatteryPercentage);
  });
}

static void charge_task(void *pvParameters) {
  while (is_charge_screen_active()) {
    update_charge_percentage();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  ESP_LOGI(TAG, "Charge screen task ended");
  charge_task_handle = NULL;
  vTaskDelete(NULL);
}

extern "C" void setup_charge_properties() {
  update_charge_percentage();
  if (charge_task_handle == NULL) {
    xTaskCreate(charge_task, "charge_task", 3072, NULL, 2, &charge_task_handle);
  }
}
