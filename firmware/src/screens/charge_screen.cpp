#include "screens/charge_screen.h"
#include "esp_log.h"
#include "remote/connection.h"
#include "remote/powermanagement.h"
#include "remote/stats.h"
#include "screens/charge_screen_state.h"
#include "ui/slint_window.h"
#include "utilities/ui_operation.h"

static const char *TAG = "PUBREMOTE-CHARGE_SCREEN";
static Screen return_screen = Screen::Stats;

static ChargeScreenState &charge_state() {
  // The first UI poll happens after power management has accepted the wake.
  static ChargeScreenState state(power_management_woke_for_charging());
  return state;
}

// UI task only, including the initial boot sample.
extern "C" void poll_charge_screen() {
  auto *window = get_slint_window();
  if (!window)
    return;
  const bool powered = power_management_is_power_connected();
  auto &charge_screen = charge_state();
  static bool last_powered = false;
  if (powered != last_powered) {
    ESP_LOGI(TAG, "UI detected charger %s", powered ? "connection" : "disconnection");
    last_powered = powered;
  }
  charge_screen.update(powered);
  const auto &state = window->global<UiState>();
  const Screen screen = state.get_screen();
  // Leaving Stats releases board-control forwarding. Defer for the entire
  // control session, including connection/reconnection and idle telemetry.
  const bool board_control_active = screen == Screen::Stats && connection_get_state() != CONNECTION_STATE_DISCONNECTED;
  if (!powered && screen == Screen::Charge) {
    if (charge_screen.should_power_off()) {
      ESP_LOGI(TAG, "Charger-only wake ended: powering off after unplug");
      enter_sleep();
      return;
    }
    state.set_screen(return_screen);
  }
  else if (charge_screen.should_show(board_control_active) && !ui_processing_active() &&
           !state.get_show_confirm_dialog() &&
           (screen == Screen::Splash || screen == Screen::Stats || screen == Screen::Menu)) {
    return_screen = screen == Screen::Splash ? Screen::Stats : screen;
    state.set_screen(Screen::Charge);
    if (state.get_screen() == Screen::Charge) {
      charge_screen.presented();
      ESP_LOGI(TAG, "Charger connected: showing charge screen");
    }
  }
  if (state.get_screen() == Screen::Charge) {
    const RemoteStats stats = stats_snapshot();
    state.set_charge_percent(stats.remoteBatteryPercentage);
    state.set_charge_label(stats.chargeState == CHARGE_STATE_DONE       ? "Charged"
                           : stats.chargeState == CHARGE_STATE_CHARGING ? "Charging"
                                                                        : "Power connected");
  }
}

extern "C" void handle_charge_tapped() {
  if (auto *window = get_slint_window(); window && window->global<UiState>().get_screen() == Screen::Charge) {
    charge_state().dismiss();
    reset_sleep_timer();
    window->global<UiState>().set_screen(return_screen);
    ESP_LOGI(TAG, "Charge screen dismissed");
  }
}
