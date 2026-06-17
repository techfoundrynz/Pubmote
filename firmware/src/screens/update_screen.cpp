#include "screens/update_screen.h"
#include "config.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ota/update_client.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/espnow.h"
#include "remote/settings.h"
#include "remote/wifi.h"
#include "generated/app-window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-UPDATE_SCREEN";

#define FORCE_UPDATE 0

enum UpdateStep {
  UPDATE_STEP_START,
  UPDATE_STEP_CONNECTING,
  UPDATE_STEP_CHECKING_UPDATE,
  UPDATE_STEP_UPDATE_AVAILABLE,
  UPDATE_STEP_NO_UPDATE,
  UPDATE_STEP_IN_PROGRESS,
  UPDATE_STEP_COMPLETE,
  UPDATE_STEP_NO_WIFI,
  UPDATE_STEP_ERROR
};

enum UpdateType {
  UPDATE_TYPE_STABLE,
  UPDATE_TYPE_PRERELEASE,
  UPDATE_TYPE_NIGHTLY
};

struct ReleaseInfo {
  UpdateType type;
  char tag_name[32];
  char name[64];
  char download_url[256];
};

static UpdateStep current_update_step = UPDATE_STEP_START;
static TaskHandle_t update_task_handle = NULL;
static ReleaseInfo available_updates[3]; // Stable, Prerelease, Nightly
static int available_update_count = 0;
static UpdateType selected_update_type = UPDATE_TYPE_STABLE;

static void update_status_ui() {
  if (!get_slint_window()) return;

  char body_text[256] = {0};
  bool show_dropdown = false;
  const char* primary_btn_text = "Next";

  switch (current_update_step) {
    case UPDATE_STEP_START: {
      char *wifi_ssid = get_wifi_ssid();
      snprintf(body_text, sizeof(body_text), "Click next to connect to %s", wifi_ssid ? wifi_ssid : "configured Wi-Fi");
      break;
    }
    case UPDATE_STEP_CONNECTING: {
      char *wifi_ssid = get_wifi_ssid();
      snprintf(body_text, sizeof(body_text), "Connecting to %s...", wifi_ssid ? wifi_ssid : "Wi-Fi");
      primary_btn_text = ""; // Hidden
      break;
    }
    case UPDATE_STEP_CHECKING_UPDATE:
      snprintf(body_text, sizeof(body_text), "Checking for updates...");
      primary_btn_text = ""; // Hidden
      break;
    case UPDATE_STEP_UPDATE_AVAILABLE: {
      snprintf(body_text, sizeof(body_text), "Choose update");
      show_dropdown = true;
      primary_btn_text = "Next";
      break;
    }
    case UPDATE_STEP_NO_UPDATE:
      snprintf(body_text, sizeof(body_text), "No updates available");
      primary_btn_text = "Exit";
      break;
    case UPDATE_STEP_IN_PROGRESS:
      snprintf(body_text, sizeof(body_text), "Downloading update...");
      primary_btn_text = ""; // Hidden
      break;
    case UPDATE_STEP_COMPLETE:
      snprintf(body_text, sizeof(body_text), "Update complete");
      primary_btn_text = "Reboot";
      break;
    case UPDATE_STEP_ERROR:
      snprintf(body_text, sizeof(body_text), "An error occurred during update");
      primary_btn_text = "Retry";
      break;
    case UPDATE_STEP_NO_WIFI:
      snprintf(body_text, sizeof(body_text), "No Wi-Fi credentials. Configure at https://pubmote.com");
      primary_btn_text = "Exit";
      break;
  }

  slint::invoke_from_event_loop([=]() {
    const auto &state = get_slint_window()->global<UiState>();
    state.set_update_body(body_text);
    state.set_update_show_dropdown(show_dropdown);
    state.set_update_primary_text(primary_btn_text);

    if (show_dropdown) {
      auto model = std::make_shared<slint::VectorModel<slint::SharedString>>();
      for (int i = 0; i < available_update_count; i++) {
        model->push_back(available_updates[i].name);
      }
      state.set_updates(model);
    }
  });
}

static void simple_progress_callback(const char *status) {
  slint::invoke_from_event_loop([=]() {
    get_slint_window()->global<UiState>().set_update_body(status);
  });
}

static void update_task(void *pvParameters) {
  connection_update_state(CONNECTION_STATE_DISCONNECTED);
  if (espnow_is_initialized()) {
    espnow_deinit();
  }
  if (!wifi_is_initialized()) {
    wifi_init();
  }

  UpdateStep last_step = current_update_step;
  char *wifi_ssid = get_wifi_ssid();
  char *wifi_password = get_wifi_password();
  int64_t last_rssi_log_time = 0;

  if (wifi_ssid == NULL || strlen(wifi_ssid) == 0 || wifi_password == NULL || strlen(wifi_password) == 0) {
    current_update_step = UPDATE_STEP_NO_WIFI;
  }

  update_status_ui();
  while (is_update_screen_active()) {
    if (current_update_step != last_step) {
      last_step = current_update_step;
      ESP_LOGI(TAG, "Update step changed: %d", current_update_step);
      update_status_ui();
    }

    switch (current_update_step) {
      case UPDATE_STEP_START:
        break;
      case UPDATE_STEP_CONNECTING: {
        esp_err_t wifi_err = ESP_OK;
        if (wifi_get_connection_state() != WIFI_STATE_CONNECTED) {
          wifi_err = wifi_connect_to_network(wifi_ssid, wifi_password);
        }
        if (wifi_get_connection_state() != WIFI_STATE_CONNECTED || wifi_err != ESP_OK) {
          current_update_step = UPDATE_STEP_ERROR;
        } else {
          ESP_LOGI(TAG, "WiFi Connected. RSSI: %d dBm", wifi_get_rssi());
          last_rssi_log_time = esp_timer_get_time();
          current_update_step = UPDATE_STEP_CHECKING_UPDATE;
        }
        break;
      }
      case UPDATE_STEP_CHECKING_UPDATE: {
        const char *asset_name = HW_TYPE;
        github_asset_urls_t result = {};
        esp_err_t err = fetch_all_asset_urls(asset_name, &result);

        if (err != ESP_OK) {
          ESP_LOGE(TAG, "Error fetching asset: %s", esp_err_to_name(err));
          current_update_step = UPDATE_STEP_ERROR;
          break;
        }

#if FORCE_UPDATE
        bool has_stable_update = result.stable_found;
        bool has_prerelease_update = result.prerelease_found;
#else
        firmware_version_t stable_version = parse_version_string(result.stable_tag);
        firmware_version_t prerelease_version = parse_version_string(result.prerelease_tag);
        firmware_version_t current_version = {.major = VERSION_MAJOR, .minor = VERSION_MINOR, .patch = VERSION_PATCH};
        bool has_stable_update = result.stable_found && is_version_greater(&stable_version, &current_version);
        bool has_prerelease_update = result.prerelease_found && is_version_greater(&prerelease_version, &current_version);
#endif

        available_update_count = 0;
        if (has_stable_update) {
          ReleaseInfo info = {};
          info.type = UPDATE_TYPE_STABLE;
          strncpy(info.tag_name, result.stable_tag, sizeof(info.tag_name) - 1);
          snprintf(info.name, sizeof(info.name), "%s", result.stable_tag);
          strncpy(info.download_url, result.stable_url, sizeof(info.download_url) - 1);
          available_updates[available_update_count++] = info;
        }

        if (has_prerelease_update) {
          ReleaseInfo info = {};
          info.type = UPDATE_TYPE_PRERELEASE;
          strncpy(info.tag_name, result.prerelease_tag, sizeof(info.tag_name) - 1);
          snprintf(info.name, sizeof(info.name), "%s (Prerelease)", result.prerelease_tag);
          strncpy(info.download_url, result.prerelease_url, sizeof(info.download_url) - 1);
          available_updates[available_update_count++] = info;
        }

        ESP_LOGI(TAG, "Available updates count: %d", available_update_count);
        if (available_update_count > 0) {
          current_update_step = UPDATE_STEP_UPDATE_AVAILABLE;
        } else {
          current_update_step = UPDATE_STEP_NO_UPDATE;
        }
        break;
      }
      case UPDATE_STEP_IN_PROGRESS: {
        ESP_LOGI(TAG, "Starting OTA update: %s", available_updates[selected_update_type].download_url);
        esp_err_t ret = apply_ota(available_updates[selected_update_type].download_url, simple_progress_callback);
        if (ret == ESP_OK) {
          current_update_step = UPDATE_STEP_COMPLETE;
          ESP_LOGI(TAG, "OTA successful");
        } else {
          ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
          current_update_step = UPDATE_STEP_ERROR;
        }
        break;
      }
      default:
        break;
    }

    if (wifi_get_connection_state() == WIFI_STATE_CONNECTED) {
      int64_t current_time = esp_timer_get_time();
      if ((current_time - last_rssi_log_time) > 1000000) {
        ESP_LOGI(TAG, "WiFi RSSI: %d dBm", wifi_get_rssi());
        last_rssi_log_time = current_time;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // Cleanup Wi-Fi / ESP-NOW
  if (wifi_is_initialized()) {
    wifi_uninit();
  }
  if (!espnow_is_initialized()) {
    espnow_init();
  }

  ESP_LOGI(TAG, "Update task ended");
  update_task_handle = NULL;
  vTaskDelete(NULL);
}

extern "C" void setup_update_properties() {
  current_update_step = UPDATE_STEP_START;
  update_status_ui();

  if (update_task_handle == NULL) {
    xTaskCreate(update_task, "update_task", 6144, NULL, 5, &update_task_handle);
  }
}

// Slint update callbacks
extern "C" void handle_update_primary() {
  switch (current_update_step) {
    case UPDATE_STEP_START:
      current_update_step = UPDATE_STEP_CONNECTING;
      break;
    case UPDATE_STEP_UPDATE_AVAILABLE:
      current_update_step = UPDATE_STEP_IN_PROGRESS;
      break;
    case UPDATE_STEP_NO_UPDATE:
    case UPDATE_STEP_NO_WIFI:
      slint::invoke_from_event_loop([]() {
        get_slint_window()->global<UiState>().set_screen(Screen::Menu);
      });
      break;
    case UPDATE_STEP_COMPLETE:
      esp_restart();
      break;
    case UPDATE_STEP_ERROR:
      current_update_step = UPDATE_STEP_START;
      break;
    default:
      current_update_step = UPDATE_STEP_START;
      break;
  }
}

extern "C" void handle_update_secondary() {
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::About);
  });
}

extern "C" void handle_update_selected(int index) {
  if (index >= 0 && index < available_update_count) {
    selected_update_type = available_updates[index].type;
  }
}

