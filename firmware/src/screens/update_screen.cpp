#include "screens/update_screen.h"
#include "build_metadata.h"
#include "config.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "ota/update_client.h"
#include "remote/display.h"
#include "remote/radio_session.h"
#include "remote/settings.h"
#include "remote/wifi.h"
#include "screens/wifi_screen.h"
#include "slint_generated/app-window.h"
#include "utilities/ui_operation.h"
#include <atomic>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

static const char *TAG = "PUBREMOTE-UPDATE_SCREEN";

#define FORCE_UPDATE 0

static constexpr uint32_t UPDATE_TASK_STACK_BYTES = 8192;

enum UpdateStep {
  UPDATE_STEP_PREPARING,
  UPDATE_STEP_START,
  UPDATE_STEP_CONNECTING,
  UPDATE_STEP_CHECKING_UPDATE,
  UPDATE_STEP_UPDATE_AVAILABLE,
  UPDATE_STEP_NO_UPDATE,
  UPDATE_STEP_IN_PROGRESS,
  UPDATE_STEP_COMPLETE,
  UPDATE_STEP_NO_WIFI,
  UPDATE_STEP_ERROR,
  UPDATE_STEP_STARTUP_ERROR
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

static std::atomic<UpdateStep> current_update_step{UPDATE_STEP_START};
static std::atomic<bool> active{false}, running{false}, owned{false}, leaving{false};
static std::atomic<unsigned> generation{0};
enum class OtaPhase {
  Idle,
  Downloading,
  Cancelled,
  Committing
};
static std::atomic<OtaPhase> ota_phase{OtaPhase::Idle};
static bool ota_begin_commit(void *) {
  OtaPhase expected = OtaPhase::Downloading;
  return ota_phase.compare_exchange_strong(expected, OtaPhase::Committing);
}
static bool ota_request_cancel() {
  OtaPhase expected = OtaPhase::Downloading;
  return ota_phase.compare_exchange_strong(expected, OtaPhase::Cancelled) || expected != OtaPhase::Committing;
}
static Screen destination = Screen::About;
static void restore_radio();
static ReleaseInfo available_updates[3]; // Stable, Prerelease, Nightly
static int available_update_count = 0;
static int selected_update_index = 0;

static void update_status_ui() {
  if (!get_slint_window() || !active)
    return;
  const unsigned token = generation.load();

  char body_text[256] = {0};
  bool show_dropdown = false;
  const char *primary_btn_text = "Next";
  bool primary_btn_enabled = true;

  switch (current_update_step.load()) {
  case UPDATE_STEP_PREPARING:
    snprintf(body_text, sizeof(body_text), "Preparing Wi-Fi...");
    primary_btn_enabled = false;
    break;
  case UPDATE_STEP_START: {
    char *wifi_ssid = get_wifi_ssid();
    snprintf(body_text, sizeof(body_text), "Click next to connect to %s", wifi_ssid ? wifi_ssid : "configured Wi-Fi");
    primary_btn_text = "Next";
    primary_btn_enabled = true;
    break;
  }
  case UPDATE_STEP_CONNECTING: {
    char *wifi_ssid = get_wifi_ssid();
    snprintf(body_text, sizeof(body_text), "Connecting to %s...", wifi_ssid ? wifi_ssid : "Wi-Fi");
    primary_btn_text = "Next";
    primary_btn_enabled = false;
    break;
  }
  case UPDATE_STEP_CHECKING_UPDATE:
    snprintf(body_text, sizeof(body_text), "Checking for updates...");
    primary_btn_text = "Next";
    primary_btn_enabled = false;
    break;
  case UPDATE_STEP_UPDATE_AVAILABLE: {
    snprintf(body_text, sizeof(body_text), "Choose update");
    show_dropdown = true;
    primary_btn_text = "Next";
    primary_btn_enabled = true;
    break;
  }
  case UPDATE_STEP_NO_UPDATE:
    snprintf(body_text, sizeof(body_text), "No updates available");
    primary_btn_text = "Exit";
    primary_btn_enabled = true;
    break;
  case UPDATE_STEP_IN_PROGRESS:
    snprintf(body_text, sizeof(body_text), "Downloading update...");
    primary_btn_text = "Next";
    primary_btn_enabled = false;
    break;
  case UPDATE_STEP_COMPLETE:
    snprintf(body_text, sizeof(body_text), "Update complete");
    primary_btn_text = "Reboot";
    primary_btn_enabled = true;
    break;
  case UPDATE_STEP_ERROR:
    snprintf(body_text, sizeof(body_text), "An error occurred during update");
    primary_btn_text = "Retry";
    primary_btn_enabled = true;
    break;
  case UPDATE_STEP_STARTUP_ERROR:
    snprintf(body_text, sizeof(body_text), "Could not start updater. Exit to restore the board connection.");
    primary_btn_text = "Exit";
    primary_btn_enabled = true;
    break;
  case UPDATE_STEP_NO_WIFI:
    snprintf(body_text, sizeof(body_text), "No Wi-Fi network saved. Set one up on this remote to check for updates.");
    primary_btn_text = "Set up Wi-Fi";
    primary_btn_enabled = true;
    break;
  }

  slint::SharedString body(body_text);
  slint::SharedString primary_text(primary_btn_text);
  slint::invoke_from_event_loop([=]() {
    if (!active || token != generation)
      return;
    const auto &state = get_slint_window()->global<UiState>();
    state.set_update_body(body);
    state.set_update_show_dropdown(show_dropdown);
    state.set_update_primary_text(primary_text);
    state.set_update_primary_enabled(primary_btn_enabled);

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
  if (!active)
    return;
  const unsigned token = generation.load();
  slint::SharedString body(status);
  slint::invoke_from_event_loop([=]() {
    if (active && token == generation)
      get_slint_window()->global<UiState>().set_update_body(body);
  });
}

static void restored(esp_err_t result, void *) {
  slint::invoke_from_event_loop([result]() {
    ui_processing_end([result]() {
      leaving = false;
      if (result == ESP_OK) {
        owned = false;
        get_slint_window()->global<UiState>().set_screen(destination);
      }
      else {
        get_slint_window()->global<UiState>().set_update_body("Could not restore board radio. Back retries.");
      }
    });
  });
}
static void restore_radio() {
  esp_err_t result = radio_session_end(restored, nullptr);
  if (result != ESP_OK)
    restored(result, nullptr);
}
static void update_task(void *pvParameters) {
  ESP_LOGI(TAG, "update_task started");
  UpdateStep last_step = current_update_step.load();
  ESP_LOGI(TAG, "Reading Wi-Fi credentials from NVS...");
  std::string ssid = get_wifi_ssid() ? get_wifi_ssid() : "";
  std::string password = get_wifi_password() ? get_wifi_password() : "";
  const char *wifi_ssid = ssid.c_str();
  const char *wifi_password = password.c_str();
  ESP_LOGI(TAG, "Read Wi-Fi credentials. SSID: %s", wifi_ssid ? wifi_ssid : "NULL");
  int64_t last_rssi_log_time = 0;

  if (wifi_ssid == NULL || strlen(wifi_ssid) == 0) {
    ESP_LOGW(TAG, "No Wi-Fi credentials found or read failed!");
    current_update_step = UPDATE_STEP_NO_WIFI;
  }

  update_status_ui();
  while (active) {
    if (current_update_step != last_step) {
      last_step = current_update_step.load();
      ESP_LOGI(TAG, "Update step changed: %d", static_cast<int>(current_update_step.load()));
      update_status_ui();
    }

    switch (current_update_step.load()) {
    case UPDATE_STEP_START:
      break;
    case UPDATE_STEP_CONNECTING: {
      esp_err_t wifi_err = ESP_OK;
      if (!wifi_is_initialized()) {
        // A previous init failed (usually internal RAM); give it one more go
        wifi_err = wifi_init();
      }
      if (wifi_err == ESP_OK && wifi_get_connection_state() != WIFI_STATE_CONNECTED) {
        wifi_err = wifi_connect_to_network_cancellable(
            wifi_ssid, wifi_password, [](void *) { return !active.load(); }, nullptr);
      }
      if (wifi_get_connection_state() != WIFI_STATE_CONNECTED || wifi_err != ESP_OK) {
        current_update_step = UPDATE_STEP_ERROR;
      }
      else {
        ESP_LOGI(TAG, "WiFi Connected. RSSI: %d dBm", wifi_get_rssi());
        last_rssi_log_time = esp_timer_get_time();
        current_update_step = UPDATE_STEP_CHECKING_UPDATE;
      }
      break;
    }
    case UPDATE_STEP_CHECKING_UPDATE: {
      const char *asset_name = HW_TYPE;
      // Keep the 1.6KB release response off the stack used by HTTPS/TLS.
      std::unique_ptr<github_asset_urls_t, decltype(&free)> result(
          static_cast<github_asset_urls_t *>(heap_caps_calloc(1, sizeof(github_asset_urls_t), MALLOC_CAP_SPIRAM)),
          &free);
      if (!result) {
        result.reset(static_cast<github_asset_urls_t *>(calloc(1, sizeof(github_asset_urls_t))));
      }
      if (!result) {
        ESP_LOGE(TAG, "Could not allocate release response");
        current_update_step = UPDATE_STEP_ERROR;
        break;
      }
      esp_err_t err = fetch_all_asset_urls(asset_name, result.get());
      ESP_LOGI(TAG, "Updater stack minimum free after release check: %u bytes",
               (unsigned)uxTaskGetStackHighWaterMark(NULL));

      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error fetching asset: %s", esp_err_to_name(err));
        current_update_step = UPDATE_STEP_ERROR;
        break;
      }

#if FORCE_UPDATE
      bool has_stable_update = result->stable_found;
      bool has_prerelease_update = result->prerelease_found;
#else
      firmware_version_t stable_version = parse_version_string(result->stable_tag);
      firmware_version_t prerelease_version = parse_version_string(result->prerelease_tag);
      firmware_version_t current_version = {.major = VERSION_MAJOR, .minor = VERSION_MINOR, .patch = VERSION_PATCH};
      bool has_stable_update = result->stable_found && is_version_greater(&stable_version, &current_version);
      bool has_prerelease_update =
          result->prerelease_found && is_version_greater(&prerelease_version, &current_version);
#endif

      available_update_count = 0;
      if (has_stable_update) {
        ReleaseInfo info = {};
        info.type = UPDATE_TYPE_STABLE;
        strlcpy(info.tag_name, result->stable_tag, sizeof(info.tag_name));
        snprintf(info.name, sizeof(info.name), "%s", result->stable_tag);
        strlcpy(info.download_url, result->stable_url, sizeof(info.download_url));
        available_updates[available_update_count++] = info;
      }

      if (has_prerelease_update) {
        ReleaseInfo info = {};
        info.type = UPDATE_TYPE_PRERELEASE;
        strlcpy(info.tag_name, result->prerelease_tag, sizeof(info.tag_name));
        snprintf(info.name, sizeof(info.name), "%s (Prerelease)", result->prerelease_tag);
        strlcpy(info.download_url, result->prerelease_url, sizeof(info.download_url));
        available_updates[available_update_count++] = info;
      }

      ESP_LOGI(TAG, "Available updates count: %d", available_update_count);
      if (available_update_count > 0) {
        current_update_step = UPDATE_STEP_UPDATE_AVAILABLE;
      }
      else {
        current_update_step = UPDATE_STEP_NO_UPDATE;
      }
      break;
    }
    case UPDATE_STEP_IN_PROGRESS: {
      ESP_LOGI(TAG, "Starting OTA update: %s", available_updates[selected_update_index].download_url);
      const ota_control_t control = {
          [](void *) { return !active.load() || ota_phase == OtaPhase::Cancelled; },
          ota_begin_commit,
          nullptr,
      };
      esp_err_t ret =
          apply_ota(available_updates[selected_update_index].download_url, simple_progress_callback, &control);
      ESP_LOGI(TAG, "Updater stack minimum free after OTA: %u bytes", (unsigned)uxTaskGetStackHighWaterMark(NULL));
      if (ret == ESP_OK) {
        current_update_step = UPDATE_STEP_COMPLETE;
        ESP_LOGI(TAG, "OTA successful");
      }
      else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
        current_update_step = UPDATE_STEP_ERROR;
      }
      ota_phase = OtaPhase::Idle;
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

  running = false;
  restore_radio();
  vTaskDelete(nullptr);
}
static void prepared(esp_err_t result, void *) {
  slint::invoke_from_event_loop([]() { ui_processing_end(); });
  if (!active) {
    running = false;
    restore_radio();
    return;
  }
  if (result == ESP_OK) {
    current_update_step = UPDATE_STEP_START;
    if (xTaskCreate(update_task, "update-task", UPDATE_TASK_STACK_BYTES, nullptr, 5, nullptr) == pdPASS)
      return;
  }
  running = false;
  current_update_step = UPDATE_STEP_STARTUP_ERROR;
  update_status_ui();
}
extern "C" void setup_update_properties() {
  ++generation;
  active = true;
  leaving = false;
  available_update_count = 0;
  selected_update_index = 0;
  const char *ssid = get_wifi_ssid();
  if (!ssid || !*ssid) {
    current_update_step = UPDATE_STEP_NO_WIFI;
    update_status_ui();
    return;
  }
  current_update_step = UPDATE_STEP_PREPARING;
  update_status_ui();
  ui_processing_begin("Preparing Wi-Fi...");
  owned = running = true;
  esp_err_t result = radio_session_begin(prepared, nullptr);
  if (result != ESP_OK) {
    ui_processing_end();
    owned = running = false;
    current_update_step = UPDATE_STEP_STARTUP_ERROR;
    update_status_ui();
  }
}
extern "C" bool update_screen_prepare_exit(int target) {
  // Cancellation and finalization compete for the same state. Once validation
  // starts, keep this page until boot selection finishes rather than accepting
  // a Back action that can no longer abort the installed image.
  if (!ota_request_cancel())
    return true;
  if (!owned) {
    active = false;
    return false;
  }
  destination = static_cast<Screen>(target);
  if (!leaving.exchange(true)) {
    active = false;
    ++generation;
    const auto &state = get_slint_window()->global<UiState>();
    state.set_update_primary_enabled(false);
    state.set_update_body("Restoring board connection...");
    ui_processing_begin("Restoring board connection...");
    if (!running)
      restore_radio();
  }
  return true;
}

// Slint update callbacks
extern "C" void handle_update_primary() {
  ESP_LOGI(TAG, "handle_update_primary called, current_update_step: %d", static_cast<int>(current_update_step.load()));
  switch (current_update_step.load()) {
  case UPDATE_STEP_START:
    ESP_LOGI(TAG, "Transitioning from UPDATE_STEP_START to UPDATE_STEP_CONNECTING");
    current_update_step = UPDATE_STEP_CONNECTING;
    break;
  case UPDATE_STEP_UPDATE_AVAILABLE:
    ESP_LOGI(TAG, "Transitioning to UPDATE_STEP_IN_PROGRESS");
    ota_phase = OtaPhase::Downloading;
    current_update_step = UPDATE_STEP_IN_PROGRESS;
    break;
  case UPDATE_STEP_NO_WIFI:
    wifi_return_to_update();
    get_slint_window()->global<UiState>().set_screen(Screen::Wifi);
    break;
  case UPDATE_STEP_NO_UPDATE:
  case UPDATE_STEP_STARTUP_ERROR:
    get_slint_window()->global<UiState>().set_screen(Screen::About);
    break;
  case UPDATE_STEP_COMPLETE:
    ESP_LOGI(TAG, "Rebooting device...");
    ui_restart();
    break;
  case UPDATE_STEP_ERROR:
    ESP_LOGI(TAG, "Transitioning back to UPDATE_STEP_START");
    current_update_step = UPDATE_STEP_START;
    break;
  default:
    ESP_LOGI(TAG, "Default fallback to UPDATE_STEP_START");
    current_update_step = UPDATE_STEP_START;
    break;
  }
}

extern "C" void handle_update_secondary() {
  get_slint_window()->global<UiState>().set_screen(Screen::About);
}

extern "C" void handle_update_selected(int index) {
  if (index >= 0 && index < available_update_count) {
    selected_update_index = index;
  }
}
