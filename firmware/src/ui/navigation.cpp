#include "ui/navigation.h"
#include "config.h"
#include "esp_system.h"
#include "remote/imu.h"
#include "remote/input_router.h"
#include "remote/input_settings.h"
#include "remote/remoteinputs.h"
#include "remote/settings_snapshot.h"
#include "screens/about_screen.h"
#include "screens/boards_screen.h"
#include "screens/charge_screen.h"
#include "screens/game_screen.h"
#include "screens/games_screen.h"
#include "screens/imu_calibration_screen.h"
#include "screens/input_calibration_screen.h"
#include "screens/menu_screen.h"
#include "screens/pairing_screen.h"
#include "screens/settings_screen.h"
#include "screens/stats_screen.h"
#include "screens/update_screen.h"
#include "screens/wifi_screen.h"
#include "ui/color_utils.h"
#include "ui/device_preferences.h"
#include "ui/display_runtime.h"
#include "ui/input_navigation.h"
#include "ui/screen_status.h"
#include "ui/slint_window.h"
#include "utilities/ui_operation.h"
#include <atomic>
// Declarations of screen handlers implemented in other files
extern "C"
{
  void handle_splash_tapped();
  void handle_stats_swiped_down();
  void handle_menu_back();
  void handle_menu_connect();
  void handle_menu_pocket_mode();
  void handle_menu_toggle_hbm();
  void handle_menu_toggle_led();
  void handle_open_settings();
  void handle_open_input_calibration();
  void handle_open_pairing();
  void handle_open_about();
  void handle_open_imu_calibration();
  void handle_menu_shutdown();
  void handle_settings_save();
  void handle_settings_changed();
  void handle_pairing_action();
  void setup_boards_properties();
  void teardown_boards_properties();
  void setup_pairing_properties();
  void teardown_pairing_properties();
  void setup_menu_properties();
  void teardown_menu_properties();
  void handle_select_board(int index);
  void handle_delete_board(int index);
  void handle_boards_back();
  void handle_boards_pair_new();
  void handle_input_calibration_primary();
  void handle_input_calibration_secondary();
  void handle_about_check_updates();
  void handle_about_back();
  void handle_imu_calibration_back();
  void handle_imu_calibration_primary();
  void handle_update_primary();
  void handle_update_secondary();
  void handle_update_selected(int index);
  void handle_open_games();
  void handle_games_back();
}

#include <algorithm>
#include <cmath>

extern "C" void handle_imu_gesture(imu_gesture_t gesture);

static std::atomic<Screen> cached_active_screen(Screen::Splash);
extern "C" bool is_stats_screen_active() {
  return cached_active_screen.load() == Screen::Stats;
}

extern "C" bool is_pairing_screen_active() {
  return cached_active_screen.load() == Screen::Pairing;
}

extern "C" bool is_about_screen_active() {
  return cached_active_screen.load() == Screen::About;
}

extern "C" bool is_imu_calibration_screen_active() {
  return cached_active_screen.load() == Screen::ImuCalibration;
}

extern "C" bool is_input_calibration_screen_active() {
  return cached_active_screen.load() == Screen::InputCalibration;
}

extern "C" bool is_menu_screen_active() {
  return cached_active_screen.load() == Screen::Menu;
}

extern "C" bool is_settings_screen_active() {
  return cached_active_screen.load() == Screen::Settings;
}

extern "C" bool is_charge_screen_active() {
  return cached_active_screen.load() == Screen::Charge;
}

extern "C" bool is_update_screen_active() {
  return cached_active_screen.load() == Screen::Update;
}

static void post_key_event(std::u8string_view key, bool press, bool release) {
  // Buttons init before the display; posting before the platform exists is fatal
  if (!ui_display_ready()) {
    return;
  }
  slint::SharedString text(key);
  slint::invoke_from_event_loop([text, press, release]() {
    if (get_slint_window() && !ui_processing_active()) {
      if (press) {
        get_slint_window()->window().dispatch_key_press_event(text);
      }
      if (release) {
        get_slint_window()->window().dispatch_key_release_event(text);
      }
    }
  });
}

static void nav_dispatch_key(std::u8string_view key) {
  post_key_event(key, true, true);
}

static void nav_focus_next() {
  nav_dispatch_key(slint::platform::key_codes::Tab);
}

static void nav_focus_previous() {
  nav_dispatch_key(slint::platform::key_codes::Backtab);
}

extern "C" void ui_dispatch_activate() {
  nav_dispatch_key(slint::platform::key_codes::Return);
}

extern "C" void ui_dispatch_activate_edge(bool pressed) {
  post_key_event(slint::platform::key_codes::Return, pressed, !pressed);
}

static void connect_callbacks() {
  const auto &state = get_slint_window()->global<UiState>();

  state.set_show_fps(SHOW_FPS);
  state.set_imu_supported(IMU_ENABLED);
  state.set_joystick_supported(input_pins_joystick_enabled());
  state.set_joystick_x_supported(input_pins_x_enabled());
  state.set_joystick_y_supported(input_pins_y_enabled());
  state.set_button_supported(input_pins_button_enabled());

  input_router_set_default(INPUT_ACTION_STICK_DOWN, nav_focus_next, input_repeat(750, 500));
  input_router_set_default(INPUT_ACTION_STICK_UP, nav_focus_previous, input_repeat(750, 500));

  state.on_screen_changed([](Screen screen) {
    Screen prev = cached_active_screen.load();
    if (prev != screen && ui_processing_active()) {
      get_slint_window()->global<UiState>().set_screen(prev);
      return;
    }
    if (prev != screen && ((prev == Screen::Wifi && wifi_screen_prepare_exit(static_cast<int>(screen))) ||
                           (prev == Screen::Update && update_screen_prepare_exit(static_cast<int>(screen))) ||
                           (prev == Screen::Pairing && pairing_screen_prepare_exit(static_cast<int>(screen))) ||
                           (prev == Screen::Game && game_screen_prepare_exit(static_cast<int>(screen))))) {
      get_slint_window()->global<UiState>().set_screen(prev);
      return;
    }
    cached_active_screen.store(screen);
    if (prev != screen) {
      // Exit hooks
      if (prev == Screen::Wifi) {
        teardown_wifi_properties();
      }
      if (prev == Screen::Stats) {
        teardown_stats_properties();
      }
      else if (prev == Screen::Pairing) {
        teardown_pairing_properties();
      }
      else if (prev == Screen::Boards) {
        teardown_boards_properties();
      }
      else if (prev == Screen::Menu) {
        teardown_menu_properties();
      }
      else if (prev == Screen::Game) {
        teardown_game_properties();
      }

      if (prev == Screen::About) {
        teardown_about_properties();
      }

      // Drops the outgoing screen's claims
      input_router_restore_defaults();

      // Enter hooks
      if (screen == Screen::Wifi) {
        setup_wifi_properties();
      }
      if (screen == Screen::Stats) {
        setup_stats_properties();
      }
      else if (screen == Screen::Menu) {
        setup_menu_properties();
      }
      else if (screen == Screen::Settings) {
        setup_settings_properties();
      }
      else if (screen == Screen::InputCalibration) {
        setup_input_calibration_properties();
      }
      else if (screen == Screen::Pairing) {
        setup_pairing_properties();
      }
      else if (screen == Screen::About) {
        setup_about_properties();
      }
      else if (screen == Screen::ImuCalibration) {
        setup_imu_calibration_properties();
      }
      else if (screen == Screen::Update) {
        setup_update_properties();
      }
      else if (screen == Screen::Boards) {
        setup_boards_properties();
      }
      else if (screen == Screen::Games) {
        setup_games_properties();
      }
      else if (screen == Screen::Game) {
        setup_game_properties();
      }
    }
  });

  state.on_splash_tapped([]() { handle_splash_tapped(); });
  state.on_charge_poll([]() { poll_charge_screen(); });
  state.on_charge_tapped([]() { handle_charge_tapped(); });
  state.on_stats_swiped_down([]() { handle_stats_swiped_down(); });
  state.on_menu_back([]() { handle_menu_back(); });
  state.on_menu_connect([]() { handle_menu_connect(); });
  state.on_menu_pocket_mode([]() { handle_menu_pocket_mode(); });
  state.on_menu_toggle_hbm([]() { handle_menu_toggle_hbm(); });
  state.on_menu_toggle_led([]() { handle_menu_toggle_led(); });
  state.on_open_settings([]() { handle_open_settings(); });
  state.on_open_input_calibration([]() { handle_open_input_calibration(); });
  state.on_open_pairing([]() { handle_open_pairing(); });
  state.on_open_about([]() { handle_open_about(); });
  state.on_open_wifi([]() { get_slint_window()->global<UiState>().set_screen(Screen::Wifi); });
  state.on_open_imu_calibration([]() { handle_open_imu_calibration(); });
  state.on_imu_calibration_back([]() { handle_imu_calibration_back(); });
  state.on_imu_calibration_primary([]() { handle_imu_calibration_primary(); });
  state.on_menu_shutdown([]() { handle_menu_shutdown(); });
  state.on_settings_save([]() { handle_settings_save(); });
  state.on_settings_changed([]() { handle_settings_changed(); });
  state.on_pairing_action([]() { handle_pairing_action(); });
  state.on_select_board([](int index) { handle_select_board(index); });
  state.on_delete_board([](int index) { handle_delete_board(index); });
  state.on_boards_back([]() { handle_boards_back(); });
  state.on_boards_pair_new([]() { handle_boards_pair_new(); });
  state.on_input_calibration_primary([]() { handle_input_calibration_primary(); });
  state.on_input_calibration_secondary([]() { handle_input_calibration_secondary(); });
  state.on_about_check_updates([]() { handle_about_check_updates(); });
  state.on_about_back([]() { handle_about_back(); });
  state.on_about_refresh([]() { update_about_stats(); });
  state.on_update_primary([]() { handle_update_primary(); });
  state.on_update_secondary([]() { handle_update_secondary(); });
  state.on_update_selected([](int index) { handle_update_selected(index); });
  state.on_open_games([]() { handle_open_games(); });
  state.on_games_launch([](int index) { handle_game_launch(index); });
  state.on_games_back([]() { handle_games_back(); });
  state.on_game_tick([]() { handle_game_tick(); });
  state.on_game_event([](int kind, float x, float y) { handle_game_event(kind, x, y); });
  state.on_game_back([]() { handle_game_back(); });

  const auto &color_slider_gen = get_slint_window()->global<ColorSliderGenerator>();
  color_slider_gen.on_generate_track(generate_color_slider_track);
}

void ui_navigation_init(int reset_reason) {
  // Surface unexpected reboots (panic / watchdog / brownout) as a dismissable
  // dialog so crashes don't go unnoticed as a silent restart
  switch (reset_reason) {
  case ESP_RST_PANIC:
    get_slint_window()->global<UiState>().set_boot_notice("The remote restarted after a firmware crash");
    break;
  case ESP_RST_TASK_WDT:
    get_slint_window()->global<UiState>().set_boot_notice("The remote restarted because a task stopped responding");
    break;
  case ESP_RST_INT_WDT:
  case ESP_RST_WDT:
    get_slint_window()->global<UiState>().set_boot_notice("The remote restarted after a watchdog timeout");
    break;
  case ESP_RST_BROWNOUT:
    get_slint_window()->global<UiState>().set_boot_notice("The remote restarted due to a power brownout");
    break;
  default:
    break;
  }
  const SettingsSnapshot snapshot = settings_snapshot();
  get_slint_window()->global<UiState>().set_calibration_prompt(
      settings_calibration_needed(&snapshot.pins, &snapshot.calibration));
  connect_callbacks();
  ui_apply_theme();
  poll_charge_screen();
}
void ui_navigation_prepare_shutdown(void) {
  imu_unregister_gesture_callback(handle_imu_gesture);
}
void ui_navigation_shutdown(void) {
  teardown_about_properties();
}
