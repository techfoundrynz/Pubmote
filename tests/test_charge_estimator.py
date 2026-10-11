"""Exercise learned profiles and their production persistence adapter."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_charge_diagnostics import ROOT, without_includes
from test_radio import function


STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
static void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
#define ESP_LOGW(...) test_log(__VA_ARGS__)
enum { CHARGE_STATE_UNKNOWN, CHARGE_STATE_CHARGING, CHARGE_STATE_DONE };
typedef struct {
  uint16_t voltage, current;
  int chargeState;
  bool isPowered, isFault;
} RemotePowerState;
static int64_t clock_us;
static int64_t esp_timer_get_time(void) { return clock_us; }
static const char *esp_err_to_name(int result) { (void)result; return "test"; }
static unsigned writes;
static bool stored_valid, reject_queue;
static unsigned char stored[8192];
static esp_err_t nvs_read_blob(const char *key, void *value, size_t size) {
  assert(strcmp(key, "charge_profile") == 0);
  if (!stored_valid) return ESP_FAIL;
  memcpy(value, stored, size);
  return ESP_OK;
}
static esp_err_t nvs_write_blob(const char *key, void *value, size_t size) {
  assert(strcmp(key, "charge_profile") == 0 && size <= sizeof(stored));
  memcpy(stored, value, size);
  ++writes;
  stored_valid = true;
  return ESP_OK;
}
static esp_err_t (*queued_work)(void *);
static void (*queued_done)(esp_err_t, void *);
static void *queued_context;
static esp_err_t radio_session_run(esp_err_t (*work)(void *),
                                  void (*done)(esp_err_t, void *), void *context) {
  if (reject_queue) return ESP_FAIL;
  assert(!queued_context);
  queued_work = work; queued_done = done; queued_context = context;
  return ESP_OK;
}
static void drain(void) {
  assert(queued_context);
  queued_done(queued_work(queued_context), queued_context);
  queued_context = NULL;
}
'''

SCENARIOS = r'''
static void missing_current(unsigned start) {
  for (unsigned t = 0; t <= 90; ++t) {
    clock_us = (int64_t)(start + t) * 1000000;
    RemotePowerState power = adc_get_power_state();
    assert(power.current == 0 && power.chargeState == CHARGE_STATE_UNKNOWN);
    charge_time_sample(&power);
    assert(!charge_time_remaining_seconds() && !queued_context);
    // A PMU with charging flags but no current measurement is also unsupported.
    power.isPowered = true;
    power.chargeState = CHARGE_STATE_CHARGING;
    charge_time_sample(&power);
    assert(!charge_time_remaining_seconds() && !queued_context);
  }
  RemotePowerState done = {4200, 0, CHARGE_STATE_DONE, true, false};
  charge_time_sample(&done);
  assert(!queued_context);
}
static void sample(unsigned seconds, bool done) {
  clock_us = (int64_t)seconds * 1000000;
  RemotePowerState power = {4200, done ? 0 : 100,
                           done ? CHARGE_STATE_DONE : CHARGE_STATE_CHARGING,
                           true, false};
  charge_time_sample(&power);
}
static void cycle(unsigned start) {
  for (unsigned t = 0; t <= 90; ++t) sample(start + t, false);
  sample(start + 91, true);
  assert(charge_time_remaining_seconds() == 0);
}
int main(void) {
  charge_time_init();
  missing_current(0);
  assert(!model.generation && !writes);
  for (unsigned t = 0; t <= 90; ++t) {
    sample(t, false);
    assert(!writes && !queued_context && !charge_time_remaining_seconds());
  }
  sample(91, true);
  assert(queued_context && !writes);
  sample(92, true);
  assert(!writes); // Completion queues one stable copy; repeated Done does not write.
  charge_time_init(); // Change live model before the queued copy is saved.
  assert(model.generation == 0);
  drain();
  assert(writes == 1);
  charge_time_init(); // A restart reloads history.
  assert(model.generation == 1);
  missing_current(100);
  assert(model.generation == 1 && writes == 1); // Even loaded history must stay hidden.
  for (unsigned t = 0; t <= 20; ++t) sample(200 + t, false);
  assert(charge_time_remaining_seconds() > 0 && writes == 1);
  charge_estimator_session_reset(&session);
  cycle(300);
  assert(queued_context && writes == 1);
  charge_time_set_persistence_enabled(false); // Factory reset precedes a pending save.
  stored_valid = false;
  memset(stored, 0, sizeof(stored));
  drain();
  assert(writes == 1 && !stored_valid);
  charge_time_init();
  assert(model.generation == 0 && !charge_time_remaining_seconds());
  charge_time_set_persistence_enabled(true); // A failed factory reset allows later saves.
  cycle(500);
  drain();
  assert(writes == 2);
  reject_queue = true;
  cycle(700); // Queue failure frees the copy and preserves the in-memory update.
  assert(!queued_context && writes == 2 && model.generation == 2);
  reject_queue = false;
  cycle(900);
  drain();
  assert(writes == 3);
  memset(stored, 0, sizeof(stored)); // Incompatible stored version is ignored.
  charge_time_init();
  assert(model.generation == 0 && !charge_time_remaining_seconds());
  puts("charge persistence tests passed");
}
'''

CONSOLE_STUBS = r'''
#include <assert.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
static unsigned resets, reboots;
static int queue_result, reset_result;
static esp_err_t (*queued_work)(void *);
static void (*queued_done)(esp_err_t, void *);
static esp_err_t radio_session_reset_settings(void) { ++resets; return reset_result; }
static void esp_restart(void) { ++reboots; }
static esp_err_t radio_session_run(esp_err_t (*work)(void *),
                                  void (*done)(esp_err_t, void *), void *context) {
  assert(!context);
  if (queue_result != ESP_OK) return queue_result;
  queued_work = work; queued_done = done;
  return ESP_OK;
}
'''

CONSOLE_SCENARIOS = r'''
int main(void) {
  queue_result = ESP_FAIL;
  assert(erase_command(0, NULL) == -1 && !resets && !reboots);
  queue_result = ESP_OK;
  assert(erase_command(0, NULL) == 0 && !resets && !reboots);
  reset_result = ESP_FAIL;
  queued_done(queued_work(NULL), NULL);
  assert(resets == 1 && !reboots); // Failed erase never reboots.
  assert(erase_command(0, NULL) == 0 && resets == 1);
  reset_result = ESP_OK;
  queued_done(queued_work(NULL), NULL);
  assert(resets == 2 && reboots == 1);
}
'''


@unittest.skipUnless(shutil.which("gcc"), "requires a host C compiler")
class ChargeEstimatorTest(unittest.TestCase):
    def test_console_reset_uses_guarded_worker(self):
        with tempfile.TemporaryDirectory(prefix="pubremote-charge-reset-") as directory:
            source, executable = Path(directory) / "test.c", Path(directory) / "test"
            console = (ROOT / "firmware/src/remote/console.c").read_text(encoding="utf-8")
            handlers = "\n".join(function(console, signature) for signature in
                                 ["static esp_err_t erase_settings(", "static void erase_finished(",
                                  "static int erase_command("])
            source.write_text(CONSOLE_STUBS + handlers + CONSOLE_SCENARIOS, encoding="utf-8")
            subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                            str(source), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_learning_and_adaptation(self):
        with tempfile.TemporaryDirectory(prefix="pubremote-estimator-") as directory:
            executable = Path(directory) / "test"
            subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                            "-I" + str(ROOT / "firmware/modules/charge_estimator/include"),
                            str(ROOT / "tests/charge_estimator_test.c"),
                            str(ROOT / "firmware/modules/charge_estimator/src/charge_estimator.c"),
                            "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_persistence_and_factory_reset(self):
        with tempfile.TemporaryDirectory(prefix="pubremote-charge-save-") as directory:
            source, executable = Path(directory) / "test.c", Path(directory) / "test"
            source.write_text(STUBS + without_includes(ROOT / "firmware/modules/charge_estimator/include/charge/charge_estimator.h")
                              + "\n" + without_includes(ROOT / "firmware/modules/charge_estimator/src/charge_estimator.c")
                              + "\n" + without_includes(ROOT / "firmware/src/charge/charge_time.c")
                              + "\nstatic uint16_t adc_get_battery_voltage(void) { return 4200; }\n"
                              + function((ROOT / "firmware/src/charge/adc/charge_driver_adc.c").read_text(encoding="utf-8"),
                                         "RemotePowerState adc_get_power_state(")
                              + SCENARIOS, encoding="utf-8")
            subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-UNDEBUG",
                            "-Werror", str(source), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
