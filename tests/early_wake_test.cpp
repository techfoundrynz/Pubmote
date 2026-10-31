#include "../firmware/src/remote/wake_button_sampler.h"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
using std::atomic_bool;
using std::atomic_store;
#define PMU_INT 14
#define BIT(n) (uint32_t(1) << (n))
#define BIT64(n) (uint64_t(1) << (n))
#define ESP_LOGI(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
constexpr int ESP_SLEEP_WAKEUP_EXT0 = 2, ESP_SLEEP_WAKEUP_EXT1 = 3, SY6970_ADDR = 0x6a;
struct InputPinSettings {
  int btn1_gpio = 3;
};
static int status_reg, reads, fault_reads;
static bool read_ok;
static int i2c_read(int address, int reg, uint8_t *value, size_t len, int timeout) {
  assert(address == SY6970_ADDR && len == 1 && timeout == 20);
  assert(reg == 0x0b || reg == 0x0c);
  ++reads;
  if (!read_ok)
    return -1;
  if (reg == 0x0c)
    ++fault_reads;
  *value = reg == 0x0b ? status_reg : 0;
  return ESP_OK;
}
#include "wake_power_probe.inc"
static bool is_power_connected, live_power, supported;
static atomic_bool power_connected_snapshot;
static uint32_t causes;
static uint64_t wake_pins;
static WakeButtonSample sample;
static int probes;
static uint32_t esp_sleep_get_wakeup_causes() {
  return causes;
}
static uint64_t esp_sleep_get_ext1_wakeup_status() {
  return wake_pins;
}
static InputPinSettings settings_get_pins() {
  return {};
}
static bool input_pins_button_enabled() {
  return true;
}
extern "C" WakeButtonSample wake_button_sampler_result(void) {
  return sample;
}
static bool charge_driver_probe_wake_power(bool *connected) {
  ++probes;
  if (!supported)
    return false;
  *connected = live_power;
  return true;
}
static bool get_button_pressed() {
  return false;
}
static void esp_task_wdt_reset() {
}
static void vTaskDelay(int) {
}
struct Sleep {};
[[noreturn]] static void begin_deep_sleep() {
  throw Sleep{};
}
#include "early_wake.inc"
static bool check(uint32_t sources, bool previous, bool current, WakeButtonSample verdict) {
  causes = sources;
  wake_pins = BIT64(3);
  is_power_connected = previous;
  live_power = current;
  sample = verdict;
  probes = 0;
  try {
    power_management_check_early_wake();
    return false;
  }
  catch (Sleep &) {
    return true;
  }
}
int main() {
  read_ok = true;
  status_reg = 4;
  reads = fault_reads = 0;
  bool connected = false;
  assert(sy6970_probe_wake_power(&connected) && connected);
  assert(reads == 3 && fault_reads == 2); // No configuration/write API is available here.
  status_reg = 0;
  assert(sy6970_probe_wake_power(&connected) && !connected);
  read_ok = false;
  connected = true;
  assert(!sy6970_probe_wake_power(&connected) && connected);
  assert(!sy6970_probe_wake_power(nullptr));

  supported = true;
  assert(check(BIT(ESP_SLEEP_WAKEUP_EXT0), true, true, WAKE_SAMPLE_UNAVAILABLE));
  assert(is_power_connected && probes == 1);
  assert(check(BIT(ESP_SLEEP_WAKEUP_EXT0), true, false, WAKE_SAMPLE_UNAVAILABLE));
  assert(!is_power_connected);
  assert(!check(BIT(ESP_SLEEP_WAKEUP_EXT0), false, true, WAKE_SAMPLE_UNAVAILABLE));
  assert(check(BIT(ESP_SLEEP_WAKEUP_EXT1), true, true, WAKE_SAMPLE_CANCELLED));
  assert(!check(BIT(ESP_SLEEP_WAKEUP_EXT1) | BIT(ESP_SLEEP_WAKEUP_EXT0), true, true, WAKE_SAMPLE_CONFIRMED));
  assert(probes == 0); // Deliberate button wake wins over PMU status.
  assert(!check(0, true, true, WAKE_SAMPLE_UNAVAILABLE) && probes == 0);
  supported = false;
  assert(!check(BIT(ESP_SLEEP_WAKEUP_EXT0), true, true, WAKE_SAMPLE_UNAVAILABLE));
}
