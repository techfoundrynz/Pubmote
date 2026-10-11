"""Exercise production Stats brightness/lifecycle and settings loading on a host."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_charge_diagnostics import ROOT
from test_radio import function

STUBS = r'''
#include "remote/settings_types.h"
#include "imu/brightness_switch.h"
#include "imu/pose_brightness.h"
#include <atomic>
#include <cassert>
#include <functional>
#include <vector>
enum class Screen { Stats, Menu, Charge };
struct UiState {
  Screen screen = Screen::Stats;
  bool dialog = false;
  Screen get_screen() const { return screen; }
  bool get_show_confirm_dialog() const { return dialog; }
  template<class T> void on_board_battery_clicked(T) const {}
  template<class T> void on_secondary_stat_left_clicked(T) const {}
};
struct Window { UiState state; template<class T> UiState &global() { return state; } } window;
Window *get_slint_window() { return &window; }
namespace slint {
std::vector<std::function<void()>> events;
void invoke_from_event_loop(std::function<void()> cb) { events.push_back(cb); }
}
static void drain() {
  auto events = std::move(slint::events); slint::events.clear();
  for (auto &event : events) event();
}
static DeviceSettings preferences;
static uint8_t brightness;
static bool pocket, processing, hbm;
static bool supports_hbm = true;
static uint64_t now_ms;
static unsigned timer_resets;
static DeviceSettings settings_get_device() { return preferences; }
static uint8_t display_get_bl_level() { return brightness; }
static void display_set_bl_level(uint8_t level) { brightness = level; }
static bool display_get_hbm() { return hbm; }
static bool display_supports_hbm() { return supports_hbm; }
static void display_set_hbm(bool active) { hbm = active; }
static bool is_pocket_mode_enabled() { return pocket; }
static bool ui_processing_active() { return processing; }
static bool is_stats_screen_active() { return window.state.screen == Screen::Stats; }
static int64_t esp_timer_get_time() { return now_ms * 1000; }
static void reset_sleep_timer() { ++timer_resets; }
static void stats_update_screen_display() {}
static void stats_register_update_cb(void (*)()) {}
static void stats_unregister_update_cb(void (*)()) {}
static void ui_save_quietly_flush() {}
static void ui_save_quietly(std::function<int()>) {}
static int save_device_settings() { return 0; }
static void settings_set_battery_display(decltype(DeviceSettings::battery_display) value) { preferences.battery_display = value; }
static void settings_set_secondary_stat_display(SecondaryStatDisplayOption value) { preferences.secondary_stat_display = value; }
enum imu_gesture_t { IMU_GESTURE_DOUBLE_TAP, IMU_GESTURE_RAISED, IMU_GESTURE_LOWERED, IMU_GESTURE_MOVED };
static void imu_register_gesture_callback(void (*)(imu_gesture_t)) {}
static void imu_unregister_gesture_callback(void (*)(imu_gesture_t)) {}
enum { INPUT_ACTION_DOUBLE_PRESS, INPUT_ACTION_STICK_UP, INPUT_ACTION_STICK_DOWN, INPUT_ONCE };
static void double_press_handler() {}
static void input_router_claim(int, void (*)(), int) {}
static void input_router_claim_board_forwarding() {}
static brightness_switch_t pose_brightness_switch;
static bool stats_raised;
static std::atomic<uint32_t> stats_session{0};
static std::atomic<bool> ui_update_pending{false};
'''

SCENARIOS = r'''
static void pose(imu_gesture_t gesture, unsigned ms) {
  now_ms = ms;
  handle_imu_gesture(gesture);
  drain();
}
int main() {
  preferences.bl_level = 200;
  preferences.dimming_mode = DIMMING_MODE_DIM;
  setup_stats_properties();
  assert(brightness == (IMU_ENABLED ? 100 : 200));
  pose(IMU_GESTURE_RAISED, 50);
  assert(brightness == 200 && !hbm);
  pose(IMU_GESTURE_LOWERED, 100);
  assert(brightness == 200); // Debounce both directions.
  pose(IMU_GESTURE_LOWERED, 550);
  assert(brightness == (IMU_ENABLED ? 100 : 200));
  preferences.bl_level = 101;
  stats_screen_refresh_brightness();
  assert(brightness == (IMU_ENABLED ? 50 : 101)); // Use new saved level immediately.
  preferences.dimming_mode = DIMMING_MODE_SCREEN_OFF;
  stats_screen_refresh_brightness();
  assert(brightness == (IMU_ENABLED ? 0 : 101));
  pose(IMU_GESTURE_RAISED, 1600);
  assert(brightness == 101 && !hbm);
  pose(IMU_GESTURE_LOWERED, 2100);
  assert(brightness == (IMU_ENABLED ? 0 : 101));
  window.state.dialog = true;
  pose(IMU_GESTURE_LOWERED, 2150);
  assert(brightness == 101); // Dialogs bypass debounce and remain readable.
  window.state.dialog = false;
  pocket = true;
  pose(IMU_GESTURE_LOWERED, 2200);
  assert(brightness == 101);
  pocket = false;
  processing = true;
  pose(IMU_GESTURE_LOWERED, 2250);
  assert(brightness == 101);
  processing = false;
  preferences.dimming_mode = DIMMING_MODE_OFF;
  stats_screen_refresh_brightness();
  assert(brightness == 101);
  preferences.dimming_mode = DIMMING_MODE_SCREEN_OFF;
  stats_screen_refresh_brightness();
  handle_imu_gesture(IMU_GESTURE_RAISED); // Queue work from the old Stats session.
  window.state.screen = Screen::Charge;
  teardown_stats_properties();
  assert(brightness == 101 && !hbm);
  brightness = 0; // Charging's independent timeout must not be overwritten.
  stats_screen_refresh_brightness();
  drain();
  assert(brightness == 0);
  window.state.screen = Screen::Stats;
  setup_stats_properties();
  handle_imu_gesture(IMU_GESTURE_RAISED);
  teardown_stats_properties();
  setup_stats_properties(); // A callback queued before re-entry must be discarded.
  drain();
  assert(brightness == (IMU_ENABLED ? 0 : 101));
  window.state.screen = Screen::Menu;
  teardown_stats_properties();
  assert(brightness == 101);
  pose(IMU_GESTURE_LOWERED, 3000);
  assert(brightness == 101);
  assert(preferences.bl_level == 101); // Temporary overrides never change saved brightness.
  window.state.screen = Screen::Stats;
  preferences.dimming_mode = DIMMING_MODE_HBM;
  setup_stats_properties();
  assert(brightness == 101 && !hbm);
  pose(IMU_GESTURE_RAISED, 4000);
  assert(hbm == !!IMU_ENABLED && brightness == 101);
  pose(IMU_GESTURE_LOWERED, 4500);
  assert(!hbm && brightness == 101);
  pose(IMU_GESTURE_RAISED, 5000);
  window.state.dialog = true;
  pose(IMU_GESTURE_RAISED, 5050);
  assert(!hbm); // Dialog suppresses automatic HBM immediately.
  window.state.dialog = false;
  supports_hbm = false;
  pose(IMU_GESTURE_RAISED, 5500);
  assert(!hbm && brightness == 101);
  supports_hbm = true;
  preferences.dimming_mode = DIMMING_MODE_HBM_ALWAYS;
  stats_screen_refresh_brightness();
  assert(hbm && brightness == 101); // Permanent HBM does not require an IMU.
  pose(IMU_GESTURE_LOWERED, 6000);
  assert(hbm && brightness == 101);
  window.state.screen = Screen::Menu;
  teardown_stats_properties();
  assert(hbm && brightness == 101); // Permanent mode survives leaving Stats.
  window.state.screen = Screen::Stats;
  setup_stats_properties();
  preferences.dimming_mode = DIMMING_MODE_DIM;
  stats_screen_refresh_brightness();
  assert(!hbm && brightness == (IMU_ENABLED ? 50 : 101));
}
'''


@unittest.skipUnless(shutil.which("gcc") and shutil.which("g++"), "requires host C/C++ compilers")
class StatsDimmingTest(unittest.TestCase):
    def test_saved_mode_loading(self):
        settings = (ROOT / "firmware/src/remote/settings.c").read_text(encoding="utf-8")
        start = settings.index('  if (nvs_read_int("dimming_mode",')
        end = settings.index('  device_settings.led_mode =', start)
        body = settings[start:end]
        stubs = r'''
#include "remote/settings_types.h"
#include "imu/pose_brightness.h"
#include <assert.h>
#include <string.h>
#define ESP_OK 0
static DeviceSettings device_settings;
static int saved = -1;
static bool supports_hbm = true;
static bool display_supports_hbm(void) { return supports_hbm; }
static int nvs_read_int(const char *key, uint32_t *out) {
  assert(strcmp(key, "dimming_mode") == 0);
  if (saved < 0) return -1;
  *out = saved;
  return ESP_OK;
}
'''
        scenarios = r'''
int main(void) {
  load(); assert(device_settings.dimming_mode == DIMMING_MODE_OFF);
  saved = 0; load(); assert(device_settings.dimming_mode == DIMMING_MODE_OFF);
  saved = 2; load(); assert(device_settings.dimming_mode == (IMU_ENABLED ? DIMMING_MODE_SCREEN_OFF : DIMMING_MODE_OFF));
  saved = 3; load(); assert(device_settings.dimming_mode == (IMU_ENABLED ? DIMMING_MODE_HBM : DIMMING_MODE_OFF));
  saved = 4; load(); assert(device_settings.dimming_mode == DIMMING_MODE_HBM_ALWAYS);
  saved = 99; load(); assert(device_settings.dimming_mode == DIMMING_MODE_OFF);
  saved = 4; supports_hbm = false; load(); assert(device_settings.dimming_mode == DIMMING_MODE_OFF);
  saved = 1; load(); assert(device_settings.dimming_mode == (IMU_ENABLED ? DIMMING_MODE_DIM : DIMMING_MODE_OFF));
}
'''
        for imu in (0, 1):
            with self.subTest(imu=imu), tempfile.TemporaryDirectory(prefix="pubremote-dimming-load-") as directory:
                source, executable = Path(directory) / "test.c", Path(directory) / "test"
                source.write_text(stubs + '\nstatic void load(void) { uint32_t temp_setting_value;\n'
                                  + body + '}\n' + scenarios, encoding="utf-8")
                subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                                f"-DIMU_ENABLED={imu}", "-I" + str(ROOT / "firmware/src"),
                                "-I" + str(ROOT / "firmware/modules/config/include"), str(source),
                                "-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True)

    def test_stats_lifecycle_with_and_without_imu(self):
        production = (ROOT / "firmware/src/screens/stats_screen.cpp").read_text(encoding="utf-8")
        handlers = "\n".join(function(production, signature) for signature in
                             ["static void update_stats_brightness(", 'extern "C" void stats_screen_refresh_brightness(',
                              "static void apply_imu_pose(", 'extern "C" void handle_imu_gesture(',
                              'extern "C" void setup_stats_properties(', 'extern "C" void teardown_stats_properties('])
        for imu in (0, 1):
            with self.subTest(imu=imu), tempfile.TemporaryDirectory(prefix="pubremote-dimming-") as directory:
                source, executable = Path(directory) / "test.cpp", Path(directory) / "test"
                source.write_text(STUBS + '\nextern "C" void handle_imu_gesture(imu_gesture_t);\n'
                                  + handlers + SCENARIOS, encoding="utf-8")
                subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                                f"-DIMU_ENABLED={imu}", "-I" + str(ROOT / "firmware/src"),
                                "-I" + str(ROOT / "firmware/modules/config/include"), str(source),
                                str(ROOT / "firmware/src/imu/brightness_switch.c"), "-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
