"""Exercise the SY6970 driver with mocked device I/O on a host compiler."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def without_includes(path):
    return "\n".join(line for line in path.read_text(encoding="utf-8").splitlines()
                     if not line.startswith("#include") and line != "#pragma once")


STUBS = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_STATE = -2;
enum RemoteChargeState { CHARGE_STATE_UNKNOWN, CHARGE_STATE_NOT_CHARGING,
                        CHARGE_STATE_CHARGING, CHARGE_STATE_DONE };
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
int64_t esp_timer_get_time() { return 123000; }
static int regs[21] = {}, fault_reads = 0, write_count = 0, failed_write = -1;
static int fault_first = 0, fault_second = 0, transport_fault_reads = 0;
struct PowersSY6970 {
  enum ChargeStatus { CHARGE_STATE_NO_CHARGE, CHARGE_STATE_PRE_CHARGE,
                      CHARGE_STATE_FAST_CHARGE, CHARGE_STATE_DONE };
  enum { TIMER_OUT_40SEC = 1 };
};
struct XPowersPPM {
  int readRegister(int r) {
    if (r == 12) return ++fault_reads % 2 ? fault_first : fault_second;
    return regs[r];
  }
  int writeRegister(int r, int v) { ++write_count; if (r == failed_write) return -1; regs[r] = v; return 0; }
  template<class... T> bool begin(T...) { return true; }
  void init() {} void enableWatchdog(int) {} void disableWatchdog() {}
  void feedWatchdog() {} void shutdown() {}
  void setSysPowerDownVoltage(int) {} void setInputCurrentLimit(int) {}
  void setChargeTargetVoltage(int) {} void setPrechargeCurr(int) {}
  void setChargerConstantCurr(int) {} void enableAutoDetectionDPDM() {}
  void disableHVDCP() {} void enableMeasure() {} void disableADCMeasure() {}
  void enableCharge() {}
  int getBattVoltage() { return 3800; } int getChargeCurrent() { return 0; }
  PowersSY6970::ChargeStatus chargeStatus() { return PowersSY6970::CHARGE_STATE_NO_CHARGE; }
};
int i2c_read(uint8_t, uint8_t reg, uint8_t* out, size_t, int) {
  if (reg == 12) ++transport_fault_reads;
  if (regs[reg] < 0) return -1;
  *out = regs[reg]; return 0;
}
int i2c_write(uint8_t, uint8_t, uint8_t*, size_t, int) { return 0; }
'''

SCENARIOS = r'''
int main() {
  regs[8] = 3;
  assert(set_ir_compensation(80, 128) == ESP_OK);
  assert(regs[8] == (3 | (4 << 5) | (4 << 2)));
  regs[8] = -1;
  int writes = write_count;
  assert(set_ir_compensation(80, 128) == ESP_FAIL && write_count == writes);

  ChargeDiagnostics diag;
  assert(sy6970_get_diagnostics(&diag) && diag.sample_ms == 0);
  assert(fault_reads == 0 && transport_fault_reads == 0);
  regs[11] = 4; fault_first = 16; fault_second = 0;
  auto state = sy6970_get_power_state();
  assert(state.isPowered && state.isFault);
  assert(sy6970_get_diagnostics(&diag));
  assert(diag.sample_ms == 123 && diag.fault_latched == 16 && diag.fault_current == 0);
  assert(diag.last_fault == 16 && diag.last_fault_ms == 123);
  assert(diag.registers[12] == 0 && diag.registers[8] == -1);
  assert(transport_fault_reads == 0 && fault_reads == 2);

  // UI reads only request/coalesce work; they do not touch hardware.
  for (int i = 0; i < 10; ++i) assert(sy6970_get_diagnostics(&diag));
  assert(fault_reads == 2);
  fault_first = 0; fault_second = 0;
  state = sy6970_get_power_state();
  assert(!state.isFault);
  assert(sy6970_get_diagnostics(&diag));
  assert(diag.fault_current == 0 && diag.last_fault == 16);

  fault_first = -1; fault_second = -1;
  state = sy6970_get_power_state();
  assert(sy6970_get_diagnostics(&diag));
  assert(diag.fault_latched == -1 && diag.fault_current == -1);
  assert(diag.registers[12] == -1 && diag.last_fault == 16);
  assert(transport_fault_reads == 0 && fault_reads == 6);
  // Shipping must refuse USB, failed reads, and failed writes, while retaining
  // unrelated REG09 configuration and selecting immediate battery cutoff.
  writes = write_count;
  assert(sy6970_enter_protection_mode() == ESP_ERR_INVALID_STATE && write_count == writes);
  regs[11] = -1;
  assert(sy6970_enter_protection_mode() == ESP_FAIL && write_count == writes);
  regs[11] = 0; regs[9] = -1;
  assert(sy6970_enter_protection_mode() == ESP_FAIL && write_count == writes);
  regs[9] = 0xDF; failed_write = 9;
  assert(sy6970_enter_protection_mode() == ESP_FAIL && regs[9] == 0xDF);
  failed_write = -1;
  assert(sy6970_enter_protection_mode() == ESP_OK && regs[9] == ((0xDF & ~8) | 32));

}
'''


@unittest.skipUnless(shutil.which("g++"), "requires a host C++ compiler")
class ChargeDiagnosticsTest(unittest.TestCase):
    def compile_and_run(self, contents):
        with tempfile.TemporaryDirectory(prefix="pubremote-charge-") as directory:
            source = Path(directory) / "test.cpp"
            executable = Path(directory) / "test"
            source.write_text(contents, encoding="utf-8")
            subprocess.run(["g++", "-std=c++17", "-UNDEBUG", str(source), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_driver_faults_and_nonblocking_snapshot(self):
        self.compile_and_run(STUBS + without_includes(ROOT / "firmware/src/charge/charge_driver.h")
                             + "\n" + without_includes(ROOT / "firmware/src/charge/sy6970/charge_driver_sy6970.cpp")
                             + SCENARIOS)

    def test_about_rows_survive_current_visibility_changes(self):
        stubs = STUBS + r'''
#include <memory>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#define VERSION_MAJOR 0
#define VERSION_MINOR 10
#define VERSION_PATCH 10
#define RELEASE_VARIANT "test"
#define HW_TYPE "host"
#define BUILD_ID "test"
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_SPIRAM 2
int heap_caps_get_free_size(int) { return 100; }
int heap_caps_get_minimum_free_size(int) { return 90; }
int heap_caps_get_largest_free_block(int) { return 80; }
enum class Screen { Menu, Update };
struct StatEntry { std::string label, value; bool is_header; };
namespace slint {
using SharedString = std::string;
template<class T> struct VectorModel {
  std::vector<T> rows;
  void push_back(T x) { rows.push_back(x); }
  size_t row_count() { return rows.size(); }
  std::optional<T> row_data(size_t i) { return rows.at(i); }
  void set_row_data(size_t i, T x) { rows.at(i) = x; }
  void insert(size_t i, T x) { rows.insert(rows.begin() + i, x); }
  void erase(size_t i) { rows.erase(rows.begin() + i); }
};
template<class T> void invoke_from_event_loop(T callback) { callback(); }
}
struct UiState {
  std::shared_ptr<slint::VectorModel<StatEntry>> model;
  void set_version_info(std::string) {}
  void set_about_stats(decltype(model) m) { model = m; }
  void set_screen(Screen) {}
};
struct Window { UiState state; template<class T> T& global() { return state; } } window;
Window* get_slint_window() { return &window; }
bool is_about_screen_active() { return true; }
struct RemoteStats { unsigned remoteBatteryVoltage = 3800, remoteBatteryPercentage = 55, chargeCurrent = 0;
                     RemoteChargeState chargeState = CHARGE_STATE_NOT_CHARGING; } telemetry;
RemoteStats stats_snapshot() { return telemetry; }
extern "C" char* charge_state_to_string(RemoteChargeState) { return const_cast<char*>("Discharging"); }
'''
        api = without_includes(ROOT / "firmware/src/charge/charge_driver.h")
        mock = r'''
static ChargeDiagnostics snapshot = {};
static bool supported = true;
bool charge_driver_get_diagnostics(ChargeDiagnostics* out) { *out = snapshot; return supported; }
'''
        scenarios = r'''
int main() {
  snapshot.chip = "SY6970";
  update_about_stats();
  auto original = stats_model;
  assert(stats_model->row_count() == 19);
  assert(stats_model->rows[9].label == "SY6970");
  assert(stats_model->rows[10].value == "Reading...");
  snapshot.sample_ms = 123; snapshot.registers[0x0B] = 0x64;
  snapshot.registers[0x03] = 0x10; snapshot.registers[0x00] = 18;
  snapshot.last_fault = 16;
  telemetry.chargeState = CHARGE_STATE_CHARGING; telemetry.chargeCurrent = 250;
  update_about_stats();
  assert(stats_model == original && stats_model->row_count() == 20);
  assert(stats_model->rows[4].label == "Current");
  assert(stats_model->rows[10].label == "SY6970");
  assert(stats_model->rows[11].value == "Good");
  assert(stats_model->rows[12].value == "USB DCP");
  assert(stats_model->rows[15].value == "1000 mA");
  assert(stats_model->rows[18].value == "Input (0x10)");
  telemetry.chargeState = CHARGE_STATE_NOT_CHARGING; telemetry.chargeCurrent = 0;
  update_about_stats();
  assert(stats_model == original && stats_model->row_count() == 19);
  assert(stats_model->rows[9].label == "SY6970");
  snapshot.registers[0x03] = -1;
  update_about_stats();
  assert(stats_model->rows[12].value == "Read error");
  teardown_about_properties(); supported = false;
  update_about_stats();
  assert(stats_model->row_count() == 9);

}
'''
        self.compile_and_run(stubs + api + mock + without_includes(ROOT / "firmware/src/screens/about_screen.cpp")
                             + scenarios)


if __name__ == "__main__":
    unittest.main()
