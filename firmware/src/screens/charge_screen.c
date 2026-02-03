#include "config.h"
#include "esp_log.h"
#include "remote/display.h"
#include "utilities/screen_utils.h"
#include "utilities/string_utils.h"
#include <remote/remoteinputs.h>
#include <remote/stats.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "PUBREMOTE-CHARGE_SCREEN";

bool is_charge_screen_active() {
  lv_obj_t *active_screen = lv_scr_act();
  // return active_screen == ui_ChargeScreen;
  return false;
}

void update_charge_labels() {
  char *formattedString;
  asprintf(&formattedString, "%d%%", remoteStats.remoteBatteryPercentage);
  // lv_label_set_text(ui_ChargeInfoLevelLabel, formattedString);
  free(formattedString);
}

void charge_task(void *pvParameters) {
  while (is_charge_screen_active()) {
    if (LVGL_lock(-1)) {
      update_charge_labels();

      LVGL_unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); // Update every 500ms
  }

  ESP_LOGI(TAG, "Charge screen task ended");
  vTaskDelete(NULL);
}

// Event handlers
void charge_screen_load_start(lv_event_t *e) {
  ESP_LOGI(TAG, "Charge screen load start");

  if (LVGL_lock(0)) {
    apply_ui_scale(NULL);

    update_charge_labels();

    LVGL_unlock();
  }
}

void charge_screen_loaded(lv_event_t *e) {
  ESP_LOGI(TAG, "Charge screen loaded");

  // Start task to update UI
  xTaskCreate(charge_task, "charge_task", 4096, NULL, 2, NULL);
}

void charge_screen_unload_start(lv_event_t *e) {
  ESP_LOGI(TAG, "Charge screen unload start");
}