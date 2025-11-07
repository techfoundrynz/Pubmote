#include "update_screen.h"
#include "config.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "ota/update_client.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/espnow.h"
#include "remote/settings.h"
#include "remote/wifi.h"
#include "utilities/screen_utils.h"
#include "utilities/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <ui/ui.h>

static const char *TAG = "PUBREMOTE-UPDATE_SCREEN";

#define FORCE_UPDATE 0

typedef enum {
  UPDATE_STEP_START,
  UPDATE_STEP_CONNECTING,
  UPDATE_STEP_CHECKING_UPDATE,
  UPDATE_STEP_UPDATE_AVAILABLE,
  UPDATE_STEP_NO_UPDATE,
  UPDATE_STEP_IN_PROGRESS,
  UPDATE_STEP_COMPLETE,
  UPDATE_STEP_NO_WIFI,
  UPDATE_STEP_ERROR
} UpdateStep;

typedef enum {
  UPDATE_TYPE_STABLE,
  UPDATE_TYPE_PRERELEASE,
  UPDATE_TYPE_NIGHTLY
} UpdateType;

typedef struct {
  UpdateType type;
  char tag_name[32];
  char name[64];
  char download_url[256];
} ReleaseInfo;

static UpdateStep current_update_step = UPDATE_STEP_START;
static TaskHandle_t update_task_handle = NULL;
static ReleaseInfo available_updates[3]; // Stable, Prerelease, Nightly
static int available_update_count = 0;
static UpdateType selected_update_type = UPDATE_TYPE_STABLE;

bool is_update_screen_active() {
  lv_obj_t *active_screen = lv_scr_act();
  return active_screen == ui_UpdateScreen;
}

static void change_update_selection(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  int selected = lv_dropdown_get_selected(obj);

  if (selected < 0 || selected >= available_update_count) {
    ESP_LOGW(TAG, "Invalid update selection index: %d", selected);
    return;
  }
  selected_update_type = available_updates[selected].type;
  ESP_LOGI(TAG, "Selected update type: %d", selected_update_type);
}

static void update_status_label() {
  ESP_LOGI(TAG, "Updating status label for step %d", current_update_step);
  static char *wifi_ssid;

  // Reset visibility
  lv_obj_clear_flag(ui_UpdatePrimaryActionButton, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_UpdateSecondaryActionButton, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(ui_UpdateSecondaryActionButtonLabel, "Cancel");
  lv_label_set_text(ui_UpdatePrimaryActionButtonLabel, "Next");
  lv_label_set_text(ui_UpdateBodyLabel, "");

  if (current_update_step == UPDATE_STEP_UPDATE_AVAILABLE) {
    // Show dropdown
    lv_obj_clear_flag(ui_UpdateBodyDropdown, LV_OBJ_FLAG_HIDDEN);
  }
  else {
    // Hide dropdown
    lv_obj_add_flag(ui_UpdateBodyDropdown, LV_OBJ_FLAG_HIDDEN);
  }

  switch (current_update_step) {
  case UPDATE_STEP_START:
    wifi_ssid = get_wifi_ssid();
    lv_label_set_text_fmt(ui_UpdateBodyLabel, "Click next to connect to %s", wifi_ssid);
    break;
  case UPDATE_STEP_CONNECTING:
    wifi_ssid = get_wifi_ssid();
    lv_label_set_text_fmt(ui_UpdateBodyLabel, "Connecting to %s...", wifi_ssid);
    lv_obj_add_flag(ui_UpdatePrimaryActionButton, LV_OBJ_FLAG_HIDDEN);
    break;
  case UPDATE_STEP_CHECKING_UPDATE:
    lv_label_set_text(ui_UpdateBodyLabel, "Checking for updates...");
    lv_obj_add_flag(ui_UpdatePrimaryActionButton, LV_OBJ_FLAG_HIDDEN);
    break;
  case UPDATE_STEP_UPDATE_AVAILABLE:
    lv_label_set_text(ui_UpdateBodyLabel, "Choose update");
    lv_label_set_text(ui_UpdatePrimaryActionButtonLabel, "Next");
    // Add lvgl dropdown
    char *available_options = strdup("");

    for (int i = 0; i < available_update_count; i++) {
      char *old = available_options;

      if (asprintf(&available_options, "%s%s%s", old,
                   (i > 0) ? "\n" : "", // Add newline if not first
                   available_updates[i].name) == -1) {
        free(old);
        return;
      }

      free(old);
    }

    lv_dropdown_set_options(ui_UpdateBodyDropdown, available_options);
    free(available_options);
    // set change callback to change_update_selection
    lv_obj_add_event_cb(ui_UpdateBodyDropdown, change_update_selection, LV_EVENT_VALUE_CHANGED, NULL);
    break;
  case UPDATE_STEP_NO_UPDATE:
    lv_label_set_text(ui_UpdateBodyLabel, "No updates available");
    lv_label_set_text(ui_UpdatePrimaryActionButtonLabel, "Exit");
    lv_obj_add_flag(ui_UpdateSecondaryActionButton, LV_OBJ_FLAG_HIDDEN);
    break;
  case UPDATE_STEP_IN_PROGRESS:
    lv_label_set_text(ui_UpdateBodyLabel, "Downloading update...");
    lv_obj_add_flag(ui_UpdatePrimaryActionButton, LV_OBJ_FLAG_HIDDEN);
    break;
  case UPDATE_STEP_COMPLETE:
    lv_label_set_text(ui_UpdateBodyLabel, "Update complete");
    lv_label_set_text(ui_UpdatePrimaryActionButtonLabel, "Reboot");
    lv_obj_add_flag(ui_UpdateSecondaryActionButton, LV_OBJ_FLAG_HIDDEN);
    break;
  case UPDATE_STEP_ERROR:
    lv_label_set_text(ui_UpdateBodyLabel, "An error occurred during the update process");
    lv_label_set_text(ui_UpdatePrimaryActionButtonLabel, "Retry");
    break;
  case UPDATE_STEP_NO_WIFI:
    lv_label_set_text(ui_UpdateBodyLabel, "No WiFi credentials found. Please configure them at https://pubmote.com");
    lv_obj_add_flag(ui_UpdatePrimaryActionButton, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui_UpdateSecondaryActionButtonLabel, "Exit");
    break;
  default:
    lv_label_set_text(ui_UpdateBodyLabel, "Unknown update step");
    break;
  }
  resize_footer_buttons(ui_UpdateFooter); // Resize footer buttons
}

static void simple_progress_callback(const char *status) {
  if (LVGL_lock(0)) {
    lv_label_set_text(ui_UpdateBodyLabel, status);
    LVGL_unlock();
  }
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

  if (wifi_ssid == NULL || strlen(wifi_ssid) == 0 || wifi_password == NULL || strlen(wifi_password) == 0) {
    current_update_step = UPDATE_STEP_NO_WIFI;
  }

  update_status_label();
  while (is_update_screen_active()) {
    bool step_has_changed = (current_update_step != last_step);
    if (step_has_changed) {
      last_step = current_update_step;
      ESP_LOGI(TAG, "Update step changed: %d", current_update_step);
      if (LVGL_lock(0)) {
        update_status_label();
        LVGL_unlock();
      }
    }

    switch (current_update_step) {
    case UPDATE_STEP_START:
      break;
    case UPDATE_STEP_CONNECTING:
      esp_err_t wifi_err = ESP_OK;
      // check if already connected
      if (wifi_get_connection_state() != WIFI_STATE_CONNECTED) {
        wifi_err = wifi_connect_to_network(wifi_ssid, wifi_password);
      }
      if (wifi_get_connection_state() != WIFI_STATE_CONNECTED || wifi_err != ESP_OK) {
        current_update_step = UPDATE_STEP_ERROR;
      }
      else {
        current_update_step = UPDATE_STEP_CHECKING_UPDATE;
      }
      break;
    case UPDATE_STEP_CHECKING_UPDATE:
      // Check for updates
      // Fetch releases from GitHub
      // If update available, current_update_step = UPDATE_STEP_UPDATE_AVAILABLE
      // Else current_update_step = UPDATE_STEP_NO_UPDATE
      const char *asset_name = HW_TYPE;
      github_asset_urls_t result = {};
      esp_err_t err = fetch_all_asset_urls(asset_name, &result);

      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error fetching asset URLs: %s", esp_err_to_name(err));
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

      if (has_stable_update) {
        ReleaseInfo stable_info = {
            .type = UPDATE_TYPE_STABLE,
        };
        strncpy(stable_info.tag_name, result.stable_tag, sizeof(stable_info.tag_name) - 1);
        snprintf(stable_info.name, sizeof(stable_info.name), "%s", result.stable_tag);
        strncpy(stable_info.download_url, result.stable_url, sizeof(stable_info.download_url) - 1);
        available_updates[available_update_count++] = stable_info;
      }

      if (has_prerelease_update) {
        ReleaseInfo prerelease_info = {
            .type = UPDATE_TYPE_PRERELEASE,
        };
        strncpy(prerelease_info.tag_name, result.prerelease_tag, sizeof(prerelease_info.tag_name) - 1);
        snprintf(prerelease_info.name, sizeof(prerelease_info.name), "%s (Prerelease)", result.prerelease_tag);
        strncpy(prerelease_info.download_url, result.prerelease_url, sizeof(prerelease_info.download_url) - 1);
        available_updates[available_update_count++] = prerelease_info;
      }

      ESP_LOGI(TAG, "Available updates count: %d", available_update_count);
      for (int i = 0; i < available_update_count; i++) {
        ESP_LOGI(TAG, "Update %d: Type=%d, Tag=%s, URL=%s", i, available_updates[i].type, available_updates[i].tag_name,
                 available_updates[i].download_url);
      }

      if (has_stable_update || has_prerelease_update) {
        current_update_step = UPDATE_STEP_UPDATE_AVAILABLE;
      }
      else {
        current_update_step = UPDATE_STEP_NO_UPDATE;
      }

      break;
    case UPDATE_STEP_UPDATE_AVAILABLE:
      // Do nothing
      break;
    case UPDATE_STEP_NO_UPDATE:
      // Do nothing
      break;
    case UPDATE_STEP_IN_PROGRESS:

      ESP_LOGI(TAG, "Starting OTA update for type %d with URL: %s", selected_update_type,
               available_updates[selected_update_type].download_url);

      esp_err_t ret = apply_ota(available_updates[selected_update_type].download_url, simple_progress_callback);
      if (ret == ESP_OK) {
        current_update_step = UPDATE_STEP_COMPLETE;
        ESP_LOGI(TAG, "OTA update successful");
      }
      else {
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(ret));
        current_update_step = UPDATE_STEP_ERROR;
      }
      break;
    case UPDATE_STEP_COMPLETE:
      break;
    case UPDATE_STEP_NO_WIFI:
      // Handle no WiFi case
      break;
    case UPDATE_STEP_ERROR:
      // Handle error case
      break;
    default:
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(LV_DISP_DEF_REFR_PERIOD));
  }

  // Cleanup
  if (wifi_is_initialized()) {
    wifi_uninit();
  }
  if (!espnow_is_initialized()) {
    espnow_init();
  }

  ESP_LOGI(TAG, "Update task ended");
  vTaskDelete(NULL);
  update_task_handle = NULL;
}

// Event handlers
void update_screen_load_start(lv_event_t *e) {
  ESP_LOGI(TAG, "Update screen load start");
  current_update_step = UPDATE_STEP_START;

  if (LVGL_lock(0)) {
    apply_ui_scale(NULL);
    create_navigation_group(ui_UpdateFooter);
    LVGL_unlock();
  }
}

void update_screen_loaded(lv_event_t *e) {
  ESP_LOGI(TAG, "Update screen loaded");
  xTaskCreate(update_task, "update_task", 8192, NULL, 2, NULL);
}

void update_screen_unload_start(lv_event_t *e) {
  ESP_LOGI(TAG, "Update screen unload start");
}

void update_primary_button_press(lv_event_t *e) {
  switch (current_update_step) {
  case UPDATE_STEP_START:
    current_update_step = UPDATE_STEP_CONNECTING;
    break;
  case UPDATE_STEP_CONNECTING:
    current_update_step = UPDATE_STEP_CHECKING_UPDATE;
    break;
  case UPDATE_STEP_CHECKING_UPDATE:
    current_update_step = UPDATE_STEP_UPDATE_AVAILABLE;
    break;
  case UPDATE_STEP_UPDATE_AVAILABLE:
    current_update_step = UPDATE_STEP_IN_PROGRESS;
    break;
  case UPDATE_STEP_NO_UPDATE:
    if (LVGL_lock(0)) {
      _ui_screen_change(&ui_MenuScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, &ui_MenuScreen_screen_init);
      LVGL_unlock();
    }
    break;
  case UPDATE_STEP_COMPLETE:
    esp_restart();
    break;
  case UPDATE_STEP_ERROR:
    current_update_step = UPDATE_STEP_START;
    break;
  default:
    ESP_LOGE(TAG, "Unknown update step: %d", current_update_step);
    current_update_step = UPDATE_STEP_START;
    break;
  }
}

void update_secondary_button_press(lv_event_t *e) {
  // Screen switch is already handled by the squareline studio generated code
}