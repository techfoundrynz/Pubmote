"""Check hidden-menu action ownership, confirmation, reset failure, and USB races."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_charge_diagnostics import ROOT, without_includes

STUBS = r'''
#include <cassert>
#include <functional>
#include <string>
constexpr int ESP_OK = 0, ESP_FAIL = -1, HAPTIC_DOUBLE_CLICK = 0;
enum class Screen { About, Extras, Games, Menu };
static bool supported = true, usb = false, shipping_failed = false;
static int reset_result = ESP_OK, resets = 0, restarts = 0, shipments = 0, haptics = 0;
bool charge_driver_supports_shipping_mode() { return supported; }
bool power_management_is_power_connected() { return usb; }
bool power_management_take_shipping_error() { bool result = shipping_failed; shipping_failed = false; return result; }
int power_management_enter_shipping_mode() { if (usb) return ESP_FAIL; ++shipments; return ESP_OK; }
int radio_session_reset_settings() { ++resets; return reset_result; }
void esp_restart() { ++restarts; }
void haptic_vibrate(int) { ++haptics; }
bool ui_operation_start(const char*, std::function<int()> work, std::function<void()> completed) {
  if (work() == ESP_OK) completed(); return true;
}
struct UiState {
  mutable Screen screen = Screen::About;
  mutable bool shipping_supported = false, shipping_available = false, dialog = false;
  mutable std::string notice, title;
  mutable std::function<void()> accept, reject;
  Screen get_screen() const { return screen; }
  void set_screen(Screen v) const { screen = v; }
  void set_shipping_mode_supported(bool v) const { shipping_supported = v; }
  void set_shipping_mode_available(bool v) const { shipping_available = v; }
  void set_boot_notice(std::string v) const { notice = v; }
  void set_show_confirm_dialog(bool v) const { dialog = v; }
  void set_confirm_dialog_title(std::string v) const { title = v; }
  void set_confirm_dialog_message(std::string) const {}
  void set_confirm_dialog_confirm_text(std::string) const {}
  void set_confirm_dialog_variant(std::string) const {}
  void on_confirm_dialog_accepted(std::function<void()> cb) const { accept = cb; }
  void on_confirm_dialog_rejected(std::function<void()> cb) const { reject = cb; }
};
struct Window { UiState state; template<class T> T& global() { return state; } } window;
Window* get_slint_window() { return &window; }
'''
SCENARIOS = r'''
int main() {
  auto &ui = window.state;
  handle_extras_factory_reset(); // About cannot initiate a hidden action.
  assert(!ui.dialog);
  handle_open_extras();
  assert(ui.screen == Screen::Extras && haptics == 1);
  setup_extras_properties();
  handle_extras_factory_reset();
  assert(ui.dialog && ui.title == "Factory reset" && resets == 0);
  ui.reject(); assert(!ui.dialog && resets == 0);
  handle_extras_factory_reset(); ui.accept();
  assert(resets == 1 && restarts == 1 && !ui.dialog);
  ui.accept(); assert(resets == 1); // Confirmation consumed once.
  reset_result = ESP_FAIL;
  handle_extras_factory_reset(); ui.accept();
  assert(resets == 2 && restarts == 1); // Never restart after a failed reset.
  supported = false; update_extras_properties();
  handle_extras_shipping_mode(); assert(!ui.dialog && shipments == 0 && !ui.shipping_supported);
  supported = true; usb = true; update_extras_properties();
  handle_extras_shipping_mode();
  assert(!ui.dialog && shipments == 0 && !ui.shipping_available && !ui.notice.empty());
  usb = false; update_extras_properties();
  handle_extras_shipping_mode(); ui.reject(); assert(shipments == 0);
  handle_extras_shipping_mode(); usb = true; ui.accept();
  assert(shipments == 0); // USB arrived during confirmation.
  usb = false;
  handle_extras_shipping_mode(); ui.accept();
  assert(shipments == 1 && !ui.dialog);
  shipping_failed = true; update_extras_properties();
  assert(!shipping_failed && ui.notice.find("failed") != std::string::npos);
  handle_extras_factory_reset(); auto stale_accept = ui.accept;
  teardown_extras_properties(); ui.set_screen(Screen::About); stale_accept();
  assert(resets == 2 && !ui.dialog); // Outgoing callbacks cannot reset.
  handle_open_extras(); setup_extras_properties(); handle_extras_back();
  assert(ui.screen == Screen::About);
}
'''

@unittest.skipUnless(shutil.which("g++"), "requires a host C++ compiler")
class ExtrasScreenTest(unittest.TestCase):
    def test_confirmations_and_action_ownership(self):
        source = STUBS + without_includes(ROOT / "firmware/src/screens/extras_screen.cpp") + SCENARIOS
        with tempfile.TemporaryDirectory(prefix="pubremote-extras-") as directory:
            path = Path(directory) / "test.cpp"
            executable = Path(directory) / "test"
            path.write_text(source)
            subprocess.run(["g++", "-std=c++17", "-UNDEBUG", str(path), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

if __name__ == "__main__":
    unittest.main()
