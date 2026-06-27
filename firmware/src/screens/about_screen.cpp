#include "screens/about_screen.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/stats.h"
#include "generated/app-window.h"
#include <stdio.h>

static const char *TAG = "PUBREMOTE-ABOUT_SCREEN";
static TaskHandle_t about_task_handle = NULL;

void update_about_version_info() {
  if (!get_slint_window()) return;

  char formattedString[128];
  snprintf(formattedString, sizeof(formattedString), "Version: %d.%d.%d.%s\nHW: %s\nHash: %s", 
           VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, RELEASE_VARIANT, HW_TYPE, BUILD_ID);

  slint::invoke_from_event_loop([=]() {
    get_slint_window()->global<UiState>().set_version_info(formattedString);
  });
}

static char last_battery_info[256] = {0};

void update_about_battery_info() {
  if (!get_slint_window()) return;

  char formattedString[256];
  int len = snprintf(formattedString, sizeof(formattedString), "Battery: %.2fV | %d%%\nState: %s", 
                     ((float)remoteStats.remoteBatteryVoltage / 1000.0f),
                     remoteStats.remoteBatteryPercentage, 
                     charge_state_to_string(remoteStats.chargeState));

  if (remoteStats.chargeState != CHARGE_STATE_NOT_CHARGING && remoteStats.chargeCurrent > 0) {
    snprintf(formattedString + len, sizeof(formattedString) - len, "\nCurrent: %umA", remoteStats.chargeCurrent);
  }

  // Only invoke the Slint event loop if the battery status text has changed
  if (strcmp(formattedString, last_battery_info) != 0) {
    snprintf(last_battery_info, sizeof(last_battery_info), "%s", formattedString);
    slint::invoke_from_event_loop([=]() {
      if (get_slint_window()) {
        get_slint_window()->global<UiState>().set_debug_info(formattedString);
      }
    });
  }
}

static void about_task(void *pvParameters) {
  while (is_about_screen_active()) {
    update_about_battery_info();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Reduced battery polling frequency to 1Hz
  }
  ESP_LOGI(TAG, "About task ended");
  about_task_handle = NULL;
  vTaskDelete(NULL);
}

extern "C" void setup_about_properties() {
  last_battery_info[0] = '\0'; // Force update on screen entry
  update_about_version_info();
  update_about_battery_info();
  
  if (about_task_handle == NULL) {
    xTaskCreate(about_task, "about_task", 3072, NULL, 2, &about_task_handle);
  }
}

// Slint event handlers
extern "C" void handle_about_back() {
  ESP_LOGI(TAG, "About back pressed");
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::Menu);
  });
}

extern "C" void handle_about_check_updates() {
  ESP_LOGI(TAG, "Check updates pressed");
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::Update);
  });
}
