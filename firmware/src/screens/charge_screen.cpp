#include "screens/charge_screen.h"
#include "charge/charge_time.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/led.h"
#include "remote/powermanagement.h"
#include "remote/settings_snapshot.h"
#include "remote/stats.h"
#include "screens/charge_screen_state.h"
#include "ui/slint_window.h"
#include "utilities/ui_operation.h"
#include <atomic>
#include <stdio.h>

static const char *TAG = "PUBREMOTE-CHARGE_SCREEN";
static Screen return_screen = Screen::Stats;
static bool boot_complete = false;
static ChargeScreenIdleState display_idle;
static std::atomic<uint32_t> charge_session{0};

static uint64_t now_ms() {
  return (uint64_t)(esp_timer_get_time() / 1000);
}

static void restore_display_preferences() {
  const auto preferences = settings_get_device();
  display_set_bl_level(preferences.bl_level);
  display_set_hbm(preferences.hbm_mode == HBM_MODE_ON);
}

static bool wake_charge_display(uint64_t event_ms) {
  // Ignore events queued before this screen became dark.
  if (!display_idle.wake_for_event(now_ms(), event_ms))
    return false;
  restore_display_preferences();
  reset_sleep_timer();
  ESP_LOGI(TAG, "Charge display awakened");
  return true;
}

extern "C" void handle_charge_imu_gesture(imu_gesture_t gesture) {
  if (gesture != IMU_GESTURE_MOVED)
    return;
  const uint32_t session = charge_session.load();
  const uint64_t event_ms = now_ms();
  slint::invoke_from_event_loop([session, event_ms]() {
    if (session != charge_session.load() || !is_charge_screen_active() || !get_slint_window())
      return;
    wake_charge_display(event_ms);
  });
}

extern "C" bool charge_screen_display_blanked() {
  return display_idle.is_blanked();
}

static void update_charging_led(RemoteChargeState state) {
  led_set_effect_charging(state == CHARGE_STATE_DONE       ? LED_CHARGING_COMPLETE
                          : state == CHARGE_STATE_CHARGING ? LED_CHARGING_ACTIVE
                                                           : LED_CHARGING_POWERED);
}

extern "C" void setup_charge_properties() {
  get_slint_window()->global<UiState>().set_charge_time_estimate("");
  charge_session.fetch_add(1);
  display_idle.enter(now_ms());
  imu_register_gesture_callback(handle_charge_imu_gesture);
  restore_display_preferences();
  update_charging_led(stats_snapshot().chargeState);
}

extern "C" void teardown_charge_properties() {
  imu_unregister_gesture_callback(handle_charge_imu_gesture);
  charge_session.fetch_add(1);
  display_idle.exit();
  restore_display_preferences();
  led_set_effect_charging(LED_CHARGING_NONE);
}

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
  else if (charge_screen.should_show(board_control_active, boot_complete) && !ui_processing_active() &&
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
    update_charging_led(stats.chargeState);
    if (display_idle.update(now_ms())) {
      display_set_bl_level(0);
      display_set_hbm(false);
      ESP_LOGI(TAG, "Charge display blanked after 30 seconds");
    }
    state.set_charge_percent(stats.remoteBatteryPercentage);
    state.set_charge_label(stats.chargeState == CHARGE_STATE_DONE       ? "Charged"
                           : stats.chargeState == CHARGE_STATE_CHARGING ? "Charging"
                                                                        : "Power connected");
    char estimate[48] = "";
    const uint32_t seconds =
        powered && stats.chargeState == CHARGE_STATE_CHARGING ? charge_time_remaining_seconds() : 0;
    if (seconds) {
      // Deliberately coarse: this is a learned approximation, not a countdown.
      const uint32_t minutes = ((seconds + 299) / 300) * 5;
      snprintf(estimate, sizeof(estimate), "About %lu min remaining", (unsigned long)minutes);
    }
    state.set_charge_time_estimate(estimate);
  }
}

extern "C" void charge_screen_finish_boot() {
  boot_complete = true;
  poll_charge_screen();
}

extern "C" void handle_charge_tapped() {
  if (auto *window = get_slint_window(); window && window->global<UiState>().get_screen() == Screen::Charge) {
    if (wake_charge_display(now_ms())) {
      return;
    }
    charge_state().dismiss();
    reset_sleep_timer();
    window->global<UiState>().set_screen(return_screen);
    ESP_LOGI(TAG, "Charge screen dismissed");
  }
}
