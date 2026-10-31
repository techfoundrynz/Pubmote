#include <cassert>
#define ESP_LOGI(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
constexpr int NOTE_SUCCESS = 1, NOTE_ERROR = 2, PMU_INT_NOTE_DURATION = 100;
static bool is_power_connected, woke_for_charging, suppress_sleep_feedback;
static bool live_power;
static int notes, last_note, samples, settled;
struct Sleep {};
static void await_pmu_int_reset() {
  ++settled;
}
static void power_state_update() {
  ++samples;
  is_power_connected = live_power;
}
static void buzzer_set_tone(int note, int) {
  ++notes;
  last_note = note;
}
static void vTaskDelay(int) {
}
static void enter_sleep_internal(bool shipping) {
  assert(!shipping);
  throw Sleep{};
}
#include "charger_wake.inc"

static bool wake(bool before_sleep, bool current) {
  is_power_connected = !current; // Qualification must use a fresh sample.
  live_power = current;
  woke_for_charging = suppress_sleep_feedback = false;
  notes = samples = settled = 0;
  try {
    return check_pmu_should_wake(before_sleep);
  }
  catch (Sleep &) {
    return false;
  }
}
int main() {
  // Status/fault/charge-completion interrupts cannot undo power-off on USB.
  assert(!wake(true, true));
  assert(!woke_for_charging && suppress_sleep_feedback && notes == 0);
  assert(samples == 1 && settled == 1);
  assert(!wake(true, true));

  // A detach records no power and sleeps; the subsequent insertion can wake.
  assert(!wake(true, false));
  assert(!is_power_connected && !woke_for_charging && last_note == NOTE_ERROR);
  assert(wake(false, true));
  assert(is_power_connected && woke_for_charging && !suppress_sleep_feedback);
  assert(notes == 1 && last_note == NOTE_SUCCESS);

  // Unpowered faults do not wake the remote or announce a charger connection.
  assert(!wake(false, false));
  assert(!woke_for_charging && notes == 0);
}
