"""Exercise the real shutdown sequence and shipping request branch with host I/O."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "firmware/src/remote/powermanagement.c").read_text()


def block(start):
    begin = SOURCE.index(start)
    opening = SOURCE.index("{", begin)
    depth = 1
    end = opening + 1
    while depth:
        depth += (SOURCE[end] == "{") - (SOURCE[end] == "}")
        end += 1
    return SOURCE[begin:end]


STUBS = r'''
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <sys/time.h>
using std::atomic_exchange;
using std::atomic_store;
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_NOT_SUPPORTED = -1, ESP_ERR_INVALID_STATE = -2;
constexpr int ESP_RST_SW = 1, ESP_PD_DOMAIN_RTC_PERIPH = 0, ESP_PD_OPTION_ON = 0;
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_ERROR_CHECK(x) assert((x) == ESP_OK)
#define pdMS_TO_TICKS(x) (x)
#define SHIPPING_ERROR_MARKER 0x53484950
#define CONNECTION_STATE_DISCONNECTED 0
#define HAPTIC_ALERT_750MS 0
#define PMU_INT 7
static bool supported = true, usb = false, shutdown_initiated = false, is_power_connected = false;
static bool suppress_sleep_feedback = false;
static std::atomic<bool> shipping_requested{false}, shipping_error{false};
static uint32_t shipping_error_marker = 0;
static bool display_stopped = false, imu_stopped = false, charger_stopped = false;
static bool peripheral_powered = true;
static int cutoff_result = ESP_OK, cutoffs = 0, wake_setup = 0;
struct Restart {}; struct Sleep {}; struct PowerRemoved {};
static bool actual_power_removed = true;
bool charge_driver_supports_shipping_mode() { return supported; }
bool power_management_is_power_connected() { return usb; }
int esp_reset_reason() { return 0; }
void esp_task_wdt_reset() {}
void connection_update_state(int) {} void unbind_power_button() {}
void haptic_vibrate(int) {} void display_off() { display_stopped = true; }
void led_set_effect_none() {} bool get_button_pressed() { return false; }
void vTaskDelay(int) {}
void imu_deinit() { imu_stopped = true; }
void haptic_deinit() {} void led_deinit() {} void buzzer_deinit() {}
void acc1_power_set_level(int level) { peripheral_powered = level != 0; } void acc2_power_set_level(int) {}
void charge_driver_deinit() { assert(peripheral_powered); charger_stopped = true; }
int charge_driver_enter_shipping_mode() {
  assert(display_stopped && imu_stopped && charger_stopped && peripheral_powered);
  ++cutoffs;
  if (cutoff_result == ESP_OK && actual_power_removed) throw PowerRemoved{};
  return cutoff_result;
}
[[noreturn]] void esp_restart() { throw Restart{}; }
void await_pmu_int_reset() {}
int enable_wake() { assert(!peripheral_powered); ++wake_setup; return ESP_OK; }
void configure_sleep_pins() {} void esp_sleep_pd_config(int, int) {}
struct InputPinSettings { int btn1_gpio = 3, btn1_active_level = 0; };
InputPinSettings settings_get_pins() { return {}; }
int wake_button_sampler_prepare(int, int) { return ESP_OK; }
[[noreturn]] void esp_deep_sleep_start() { throw Sleep{}; }
void power_state_update() { assert(peripheral_powered); is_power_connected = usb; }
'''


@unittest.skipUnless(shutil.which("g++"), "requires a host C++ compiler")
class ShippingModeTest(unittest.TestCase):
    def test_shutdown_order_and_usb_recheck(self):
        source = STUBS + block("esp_err_t power_management_enter_shipping_mode()")
        source += block("bool power_management_take_shipping_error()")
        source += block("static void begin_deep_sleep(void)")
        source += block("static void enter_sleep_internal(bool shipping)")
        source += "void process_request() {" + block("if (atomic_exchange(&shipping_requested, false))") + "}"
        source += r'''
int main() {
  supported = false;
  assert(power_management_enter_shipping_mode() == ESP_ERR_NOT_SUPPORTED);
  supported = true; usb = true;
  assert(power_management_enter_shipping_mode() == ESP_ERR_INVALID_STATE && !shipping_requested);
  usb = false;
  assert(power_management_enter_shipping_mode() == ESP_OK);
  usb = true; process_request(); // USB arrived after confirmation.
  assert(cutoffs == 0 && !display_stopped && power_management_take_shipping_error());
  assert(!power_management_take_shipping_error());
  usb = false;
  assert(power_management_enter_shipping_mode() == ESP_OK);
  try { process_request(); assert(false); } catch (PowerRemoved&) {}
  assert(cutoffs == 1 && wake_setup == 0);
  // A rejected/failed cutoff restores firmware by restart, never ordinary sleep.
  cutoff_result = -3;
  try { enter_sleep_internal(true); assert(false); } catch (Restart&) {}
  assert(shipping_error_marker == SHIPPING_ERROR_MARKER && wake_setup == 0);
  cutoff_result = ESP_OK; actual_power_removed = false; shipping_error_marker = 0;
  try { enter_sleep_internal(true); assert(false); } catch (Restart&) {}
  assert(shipping_error_marker == SHIPPING_ERROR_MARKER);
  // Normal shutdown retains the usual wake configuration and does not cut battery power.
  const int before = cutoffs;
  try { enter_sleep_internal(false); assert(false); } catch (Sleep&) {}
  assert(cutoffs == before && wake_setup == 1);
}
'''
        with tempfile.TemporaryDirectory(prefix="pubremote-shipping-") as directory:
            path = Path(directory) / "test.cpp"
            executable = Path(directory) / "test"
            path.write_text(source)
            subprocess.run(["g++", "-std=c++17", "-UNDEBUG", str(path), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
