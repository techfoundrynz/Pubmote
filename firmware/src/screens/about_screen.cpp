#include "screens/about_screen.h"
#include "config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/stats.h"
#include "generated/app-window.h"
#include <memory>
#include <stdio.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-ABOUT_SCREEN";
static TaskHandle_t about_task_handle = NULL;

void update_about_version_info() {
  if (!get_slint_window()) return;

  char formattedString[128];
  snprintf(formattedString, sizeof(formattedString), "Version: %d.%d.%d.%s\nHW: %s\nHash: %s", 
           VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, RELEASE_VARIANT, HW_TYPE, BUILD_ID);

  slint::SharedString version_info(formattedString);
  slint::invoke_from_event_loop([=]() {
    get_slint_window()->global<UiState>().set_version_info(version_info);
  });
}

static char last_stats_signature[512] = {0};

static void add_row(std::shared_ptr<slint::VectorModel<StatEntry>> &model, const char *label, const char *value) {
  StatEntry entry;
  entry.label = label;
  entry.value = value;
  entry.is_header = false;
  model->push_back(entry);
}

static void add_header(std::shared_ptr<slint::VectorModel<StatEntry>> &model, const char *label) {
  StatEntry entry;
  entry.label = label;
  entry.value = "";
  entry.is_header = true;
  model->push_back(entry);
}

// Rebuilt whole rather than diffed per row: the list is short, and a signature check keeps
// the event loop out of it unless something actually changed.
void update_about_stats() {
  if (!get_slint_window())
    return;

  char voltage[16], level[16], internal_free[16], min_ever[16], largest[16], psram[16], current[16];
  snprintf(voltage, sizeof(voltage), "%.2f V", (float)remoteStats.remoteBatteryVoltage / 1000.0f);
  snprintf(level, sizeof(level), "%d%%", remoteStats.remoteBatteryPercentage);
  snprintf(current, sizeof(current), "%u mA", remoteStats.chargeCurrent);
  snprintf(internal_free, sizeof(internal_free), "%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  snprintf(min_ever, sizeof(min_ever), "%u", (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  snprintf(largest, sizeof(largest), "%u", (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  snprintf(psram, sizeof(psram), "%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  const char *state = charge_state_to_string(remoteStats.chargeState);
  bool show_current = remoteStats.chargeState != CHARGE_STATE_NOT_CHARGING && remoteStats.chargeCurrent > 0;

  char signature[512];
  snprintf(signature, sizeof(signature), "%s|%s|%s|%s|%d|%s|%s|%s|%s", voltage, level, state, current,
           (int)show_current, internal_free, min_ever, largest, psram);
  if (strcmp(signature, last_stats_signature) == 0) {
    return;
  }
  snprintf(last_stats_signature, sizeof(last_stats_signature), "%s", signature);

  auto model = std::make_shared<slint::VectorModel<StatEntry>>();
  add_header(model, "BATTERY");
  add_row(model, "Voltage", voltage);
  add_row(model, "Level", level);
  add_row(model, "State", state);
  if (show_current) {
    add_row(model, "Current", current);
  }
  add_header(model, "MEMORY");
  add_row(model, "Internal free", internal_free);
  add_row(model, "Min ever", min_ever);
  add_row(model, "Largest block", largest);
  add_row(model, "PSRAM free", psram);

  slint::invoke_from_event_loop([=]() {
    if (get_slint_window()) {
      get_slint_window()->global<UiState>().set_about_stats(model);
    }
  });
}

static void about_task(void *pvParameters) {
  while (is_about_screen_active()) {
    update_about_stats();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Reduced battery polling frequency to 1Hz
  }
  ESP_LOGI(TAG, "About task ended");
  about_task_handle = NULL;
  vTaskDelete(NULL);
}

extern "C" void setup_about_properties() {
  last_stats_signature[0] = '\0'; // Force update on screen entry
  update_about_version_info();
  update_about_stats();
  
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
