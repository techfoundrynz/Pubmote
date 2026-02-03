#include "config.h"
#include "esp_log.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/espnow.h"
#include "remote/wifi.h"
#include "utilities/screen_utils.h"
#include "utilities/string_utils.h"
#include <remote/remoteinputs.h>
#include <remote/stats.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "PUBREMOTE-ABOUT_SCREEN";

bool is_about_screen_active() {
  lv_obj_t *active_screen = lv_scr_act();
  // return active_screen == ui_AboutScreen;
  return false;
}

// Set version label string
static void update_version_info_label() {
  char *formattedString;
  asprintf(&formattedString, "Version: %d.%d.%d.%s\nHW: %s\nHash: %s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH,
           RELEASE_VARIANT, HW_TYPE, BUILD_ID);
  // lv_label_set_text(ui_VersionInfoLabel, formattedString);
  free(formattedString);
}

static void update_battery_percentage_label() {
  char *formattedString;
  asprintf(&formattedString, "Battery: %.2fV | %d%%\nState: %s", ((float)remoteStats.remoteBatteryVoltage / 1000.0f),
           remoteStats.remoteBatteryPercentage, charge_state_to_string(remoteStats.chargeState));

  if (remoteStats.chargeState != CHARGE_STATE_NOT_CHARGING && remoteStats.chargeCurrent > 0) {
    asprintf(&formattedString, "%s\nCurrent: %umA", formattedString, remoteStats.chargeCurrent);
  }

  // lv_label_set_text(ui_DebugInfoLabel, formattedString);
  free(formattedString);
}

static void about_task(void *pvParameters) {
  while (is_about_screen_active()) {
    if (LVGL_lock(-1)) {
      update_battery_percentage_label();

      LVGL_unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }

  ESP_LOGI(TAG, "About task ended");
  vTaskDelete(NULL);
}

// Event handlers
void about_screen_load_start(lv_event_t *e) {
  ESP_LOGI(TAG, "About screen load start");

  if (LVGL_lock(0)) {
    // apply_ui_scale(NULL);
    // update_version_info_label();
    // update_battery_percentage_label();
    // lv_obj_add_event_cb(ui_AboutBody, paged_scroll_event_cb, LV_EVENT_SCROLL, ui_AboutHeader);
    // add_page_scroll_indicators(ui_AboutHeader, ui_AboutBody);
    // create_navigation_group(ui_AboutFooter);

    LVGL_unlock();
  }
}

void about_screen_loaded(lv_event_t *e) {
  ESP_LOGI(TAG, "About screen loaded");

  // Start task to update UI
  xTaskCreate(about_task, "about_task", 3072, NULL, 2, NULL);
}

void about_screen_unload_start(lv_event_t *e) {
  ESP_LOGI(TAG, "About screen unload start");
  // lv_obj_remove_event_cb(ui_AboutBody, paged_scroll_event_cb);
}