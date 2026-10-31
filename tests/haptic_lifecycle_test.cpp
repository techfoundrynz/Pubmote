#include <atomic>
#include <cassert>
using std::atomic_bool;
using std::atomic_load;
using std::atomic_store;
using std::atomic_exchange;
#define HAPTIC_ENABLED 1
#define ESP_LOGI(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
constexpr int ESP_OK = 0;
enum HapticFeedbackPattern { HAPTIC_ALERT_750MS };
static int init_calls, play_calls, stop_calls, free_calls, callbacks, delay_ms;
static bool driver_ok = true;
static int haptic_driver_init() { ++init_calls; return driver_ok ? ESP_OK : -1; }
static void haptic_driver_play_vibration(HapticFeedbackPattern) { ++play_calls; }
static void haptic_driver_stop_vibration() { ++stop_calls; }
static void haptic_driver_deinit() { ++free_calls; }
static void vTaskDelay(int ms) { delay_ms += ms; }
static void register_startup_cb(void (*)()) { ++callbacks; }
#include "haptic.inc"
int main() {
  haptic_vibrate(HAPTIC_ALERT_750MS);
  haptic_stop_vibration();
  haptic_deinit(); // Early rejected wake never initialized a motor.
  assert(init_calls == 0 && play_calls == 0 && stop_calls == 0 && free_calls == 0 && delay_ms == 0);
  haptic_init();
  haptic_init();
  haptic_play_startup_effect();
  haptic_deinit();
  haptic_deinit();
  assert(init_calls == 1 && callbacks == 1 && play_calls == 1 && stop_calls == 1 && free_calls == 1);
  assert(delay_ms == 10);
  driver_ok = false;
  haptic_init();
  haptic_vibrate(HAPTIC_ALERT_750MS);
  haptic_deinit();
  assert(init_calls == 2 && callbacks == 1 && play_calls == 1 && free_calls == 1);
}
