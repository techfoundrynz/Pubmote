#include "screens/menu_screen.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/powermanagement.h"
#include "remote/settings.h"
#include "generated/app-window.h"

static const char *TAG = "PUBREMOTE-MENU_SCREEN";

extern "C" bool display_get_hbm();
extern "C" void display_set_hbm(bool active);

extern "C" void setup_menu_properties() {
  if (!get_slint_window()) return;
  
  const auto &state = get_slint_window()->global<UiState>();
  
  if (pairing_state == PAIRING_STATE_PAIRED) {
    state.set_menu_show_connect(true);
  } else {
    state.set_menu_show_connect(false);
  }

  if (is_pocket_mode_enabled()) {
    state.set_pocket_mode_text("Disable Pocket Mode");
  } else {
    state.set_pocket_mode_text("Enable Pocket Mode");
  }

  if (display_get_hbm()) {
    state.set_hbm_mode_text("Disable HBM Mode");
  } else {
    state.set_hbm_mode_text("Enable HBM Mode");
  }
}


extern "C" void handle_menu_connect() {
  ESP_LOGI(TAG, "Connect button pressed");
  if (connection_state == CONNECTION_STATE_DISCONNECTED) {
    connection_connect_to_default_peer();
  } else {
    connection_update_state(CONNECTION_STATE_DISCONNECTED);
  }
  
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::Stats);
  });
}

extern "C" void handle_menu_pocket_mode() {
  ESP_LOGI(TAG, "Pocket mode button pressed");
  if (device_settings.pocket_mode == POCKET_MODE_DISABLED) {
    device_settings.pocket_mode = POCKET_MODE_ENABLED;
  } else {
    device_settings.pocket_mode = POCKET_MODE_DISABLED;
  }
  save_device_settings();

  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::Stats);
  });
}

extern "C" void handle_menu_toggle_hbm() {
  ESP_LOGI(TAG, "HBM mode button pressed");
  display_set_hbm(!display_get_hbm());
  setup_menu_properties(); // update the text
}

// Forward declarations of screen setup functions
extern "C" {
void setup_settings_properties();
void setup_calibration_properties();
void setup_pairing_properties();
void setup_about_properties();
}

extern "C" void handle_open_settings() {
  ESP_LOGI(TAG, "Open settings pressed");
  setup_settings_properties();
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::Settings);
  });
}

extern "C" void handle_open_calibration() {
  ESP_LOGI(TAG, "Open calibration pressed");
  setup_calibration_properties();
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::Calibration);
  });
}

extern "C" void handle_open_pairing() {
  ESP_LOGI(TAG, "Open pairing pressed");
  setup_pairing_properties();
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::Pairing);
  });
}

extern "C" void handle_open_about() {
  ESP_LOGI(TAG, "Open about pressed");
  setup_about_properties();
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::About);
  });
}

extern "C" void handle_menu_shutdown() {
  ESP_LOGI(TAG, "Shutdown button pressed");
  enter_sleep();
}
