#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utilities/screen_utils.h"
#include <remote/connection.h>
#include <remote/display.h>
#include <remote/powermanagement.h>
#include <remote/settings.h>

static const char *TAG = "PUBREMOTE-MENU_SCREEN";

static bool confirm_reset = false;

static void set_reset_mode(bool mode) {
  // confirm_reset = mode;
  // if (confirm_reset) {
  //   lv_label_set_text(ui_MenuShutdownButtonLabel, "Factory reset?");
  // }
  // else {
  //   lv_label_set_text(ui_MenuShutdownButtonLabel, "Shutdown");
  // }
}

bool is_menu_screen_active() {
  lv_obj_t *active_screen = lv_scr_act();
  // return active_screen == ui_MenuScreen;
  return false;
}

// Event handlers
void menu_screen_load_start(lv_event_t *e) {
  ESP_LOGI(TAG, "Menu screen load start");
  // Permanent screen - don't apply scale

  if (LVGL_lock(0)) {
    // create_navigation_group(ui_MenuBody);
    LVGL_unlock();
  }
}

void menu_screen_loaded(lv_event_t *e) {
  ESP_LOGI(TAG, "Menu screen loaded");

  set_reset_mode(false);

  // if (LVGL_lock(0)) {
  //   if (pairing_state == PAIRING_STATE_PAIRED) {
  //     lv_obj_clear_flag(ui_MenuConnectButton, LV_OBJ_FLAG_HIDDEN);

  //     if (connection_state == CONNECTION_STATE_DISCONNECTED) {
  //       lv_label_set_text(ui_MenuConnectButtonLabel, "Connect");
  //     }
  //     else {
  //       lv_label_set_text(ui_MenuConnectButtonLabel, "Disconnect");
  //     }
  //   }
  //   else {
  //     lv_obj_add_flag(ui_MenuConnectButton, LV_OBJ_FLAG_HIDDEN);
  //   }

  //   // Update pocket mode button text
  //   if (is_pocket_mode_enabled()) {
  //     lv_label_set_text(ui_MenuPocketModeButtonLabel, "Disable Pocket Mode");
  //   }
  //   else {
  //     lv_label_set_text(ui_MenuPocketModeButtonLabel, "Enable Pocket Mode");
  //   }

  //   LVGL_unlock();
  // }
}

void menu_screen_unload_start(lv_event_t *e) {
  ESP_LOGI(TAG, "Menu screen unload start");
}

void enter_deep_sleep(lv_event_t *e) {
  enter_sleep();
}

void menu_connect_press(lv_event_t *e) {
  ESP_LOGI(TAG, "Connect button pressed");

  if (connection_state == CONNECTION_STATE_DISCONNECTED) {
    connection_connect_to_default_peer();
  }
  else {
    connection_update_state(CONNECTION_STATE_DISCONNECTED);
  }

  if (LVGL_lock(0)) {
    // _ui_screen_change(&ui_StatsScreen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_StatsScreen_screen_init);
    LVGL_unlock();
  }
}

void menu_pocket_mode_press(lv_event_t *e) {
  ESP_LOGI(TAG, "Pocket mode button pressed");

  // Toggle pocket mode
  if (device_settings.pocket_mode == POCKET_MODE_DISABLED) {
    device_settings.pocket_mode = POCKET_MODE_ENABLED;
  }
  else {
    device_settings.pocket_mode = POCKET_MODE_DISABLED;
  }

  save_device_settings();

  // if (LVGL_lock(0)) {
  //   _ui_screen_change(&ui_StatsScreen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_StatsScreen_screen_init);
  //   LVGL_unlock();
  // }
}

// Track press event so we can bind long and short press events without overlap
static bool is_press_handled = false;

void shutdown_button_press(lv_event_t *e) {
  if (is_press_handled) {
    ESP_LOGI(TAG, "Shutdown button press already handled");
    return;
  }

  ESP_LOGI(TAG, "Shutdown button press");
  if (!confirm_reset) {
    enter_sleep();
  }
  else {
    reset_all_settings();
    esp_restart();
  }
}

void shutdown_button_long_press(lv_event_t *e) {
  if (is_press_handled) {
    ESP_LOGI(TAG, "Shutdown button long press already handled");
    return;
  }
  ESP_LOGI(TAG, "Shutdown button long press");
  set_reset_mode(!confirm_reset);
  is_press_handled = true;
}

void shutdown_button_down(lv_event_t *e) {
  is_press_handled = false;
}