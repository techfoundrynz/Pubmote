#include "screens/menu_screen.h"
#include "config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "generated/app-window.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/powermanagement.h"
#include "remote/settings.h"
#include "remote/stats.h"

static const char *TAG = "PUBREMOTE-MENU_SCREEN";

static bool confirm_reset = false;

enum PendingAction {
  PENDING_NONE,
  PENDING_SHUTDOWN,
  PENDING_RESET
};
static PendingAction pending_action = PENDING_NONE;

extern "C" void handle_menu_shutdown_long_press() {
  confirm_reset = !confirm_reset;
  slint::invoke_from_event_loop(
      []() { get_slint_window()->global<UiState>().set_shutdown_text(confirm_reset ? "Factory reset?" : "Shutdown"); });
}

static void menu_update_display() {
  if (!get_slint_window())
    return;
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_connection_state((int)connection_state);
  });
}

extern "C" void setup_menu_properties() {
  if (!get_slint_window())
    return;

  const auto &state = get_slint_window()->global<UiState>();

  confirm_reset = false;
  pending_action = PENDING_NONE;
  state.set_shutdown_text("Shutdown");
  state.on_menu_shutdown_long_press([]() { handle_menu_shutdown_long_press(); });

  state.on_confirm_dialog_accepted([]() {
    slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_show_confirm_dialog(false); });
    if (pending_action == PENDING_RESET) {
      reset_all_settings();
      esp_restart();
    }
    else if (pending_action == PENDING_SHUTDOWN) {
      enter_sleep();
    }
    pending_action = PENDING_NONE;
  });

  state.on_confirm_dialog_rejected([]() {
    slint::invoke_from_event_loop([]() {
      const auto &state = get_slint_window()->global<UiState>();
      state.set_show_confirm_dialog(false);
      if (confirm_reset) {
        confirm_reset = false;
        state.set_shutdown_text("Shutdown");
      }
    });
    pending_action = PENDING_NONE;
  });

  if (pairing_state == PAIRING_STATE_PAIRED) {
    state.set_menu_show_connect(true);
  }
  else {
    state.set_menu_show_connect(false);
  }

  state.set_pocket_mode_active(is_pocket_mode_enabled());
  state.set_hbm_mode((int)device_settings.hbm_mode);
  state.set_hbm_mode_supported(display_supports_hbm());

  stats_register_update_cb(menu_update_display);
  menu_update_display();
}

extern "C" void handle_menu_connect() {
  ESP_LOGI(TAG, "Connect button pressed");
  if (connection_state == CONNECTION_STATE_DISCONNECTED) {
    connection_connect_to_default_peer();
  }
  else {
    connection_update_state(CONNECTION_STATE_DISCONNECTED);
  }

  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Stats); });
}

extern "C" void handle_menu_pocket_mode() {
  ESP_LOGI(TAG, "Pocket mode button pressed");
  if (device_settings.pocket_mode == POCKET_MODE_DISABLED) {
    device_settings.pocket_mode = POCKET_MODE_ENABLED;
  }
  else {
    device_settings.pocket_mode = POCKET_MODE_DISABLED;
  }
  save_device_settings();

  setup_menu_properties();
}

extern "C" void handle_menu_toggle_hbm() {
  if (!display_supports_hbm()) {
    return;
  }
  ESP_LOGI(TAG, "HBM mode button pressed");
#if IMU_ENABLED
  device_settings.hbm_mode = (HbmModeOptions)((device_settings.hbm_mode + 1) % 3);
#else
  if (device_settings.hbm_mode == HBM_MODE_OFF) {
    device_settings.hbm_mode = HBM_MODE_ON;
  }
  else {
    device_settings.hbm_mode = HBM_MODE_OFF;
  }
#endif
  save_device_settings();

  // Apply immediately: HBM is only active if HBM mode is set to ON
  if (device_settings.hbm_mode == HBM_MODE_ON) {
    display_set_hbm(true);
  }
  else {
    display_set_hbm(false);
  }

  setup_menu_properties(); // update the text
}

extern "C" void handle_open_settings() {
  ESP_LOGI(TAG, "Open settings pressed");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Settings); });
}

extern "C" void handle_open_input_calibration() {
  ESP_LOGI(TAG, "Open input calibration pressed");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::InputCalibration); });
}

extern "C" void handle_open_pairing() {
  ESP_LOGI(TAG, "Open pairing pressed");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Boards); });
}

extern "C" void handle_open_about() {
  ESP_LOGI(TAG, "Open about pressed");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::About); });
}

extern "C" void handle_menu_shutdown() {
  ESP_LOGI(TAG, "Shutdown button pressed");
  slint::invoke_from_event_loop([]() {
    const auto &state = get_slint_window()->global<UiState>();
    if (confirm_reset) {
      pending_action = PENDING_RESET;
      state.set_confirm_dialog_title("Factory Reset");
      state.set_confirm_dialog_message("This will wipe all settings. Proceed?");
      state.set_confirm_dialog_confirm_text("Reset");
      state.set_confirm_dialog_variant("danger");
    }
    else {
      pending_action = PENDING_SHUTDOWN;
      state.set_confirm_dialog_title("Power Off");
      state.set_confirm_dialog_message("Are you sure you want to turn off the remote?");
      state.set_confirm_dialog_confirm_text("Shutdown");
    }
    state.set_show_confirm_dialog(true);
  });
}

extern "C" void teardown_menu_properties() {
  ESP_LOGI(TAG, "Tearing down menu screen properties");
  stats_unregister_update_cb(menu_update_display);
  if (!get_slint_window())
    return;
  slint::invoke_from_event_loop([]() {
    const auto &state = get_slint_window()->global<UiState>();
    state.on_menu_shutdown_long_press([]() {});
    state.on_confirm_dialog_accepted([]() {});
    state.on_confirm_dialog_rejected([]() {});
  });
}
