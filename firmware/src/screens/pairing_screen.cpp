#include "screens/pairing_screen.h"
#include "esp_log.h"
#include "generated/app-window.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/led.h"

static const char *TAG = "PUBREMOTE-PAIRING_SCREEN";

extern "C" void setup_pairing_properties() {
  ESP_LOGI(TAG, "Setting up pairing screen properties");
  led_set_effect_rainbow();
  connection_update_state(CONNECTION_STATE_DISCONNECTED);
  pairing_state = PAIRING_STATE_UNPAIRED;

  slint::invoke_from_event_loop([]() {
    const auto &state = get_slint_window()->global<UiState>();
    state.set_pairing_code("0000");
    state.set_pairing_action_text("Cancel");
  });
}

extern "C" void handle_pairing_action() {
  ESP_LOGI(TAG, "Pairing cancel action");
  pairing_state = PAIRING_STATE_UNPAIRED;

  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Boards); });
}

extern "C" void teardown_pairing_properties() {
  ESP_LOGI(TAG, "Tearing down pairing screen properties");
  led_set_effect_default();
}
