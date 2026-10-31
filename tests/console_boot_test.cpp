#include <cassert>
#include <initializer_list>

#define DEBUG_MEMORY 0
#define ORCHESTRATOR_PERIOD_MS 250
#define pdMS_TO_TICKS(ms) (ms)

static bool splash;
static int waits, starts;
struct PollComplete {};
static bool is_splash_screen_active() {
  return splash;
}
static void vTaskDelay(int ms) {
  assert(ms == 20 && starts == 0);
  if (++waits == 3)
    splash = false;
}
static void console_init() {
  assert(!splash);
  ++starts;
}
static void console_poll_usb() {
  assert(starts == 1 && !splash);
  throw PollComplete{};
}
#include "orchestrator_task.inc"

int main() {
  for (bool initially_visible : {false, true}) {
    splash = initially_visible;
    waits = starts = 0;
    try {
      orchestrator_task(nullptr);
      assert(false);
    }
    catch (const PollComplete &) {
    }
    assert(starts == 1 && waits == (initially_visible ? 3 : 0));
  }
}
