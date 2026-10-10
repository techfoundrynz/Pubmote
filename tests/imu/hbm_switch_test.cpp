#include "imu/hbm_switch.h"
#include <assert.h>
#include <stdio.h>

static hbm_switch_t state;
static bool active;
static bool request(bool desired, uint64_t ms) {
  bool changed = hbm_switch_should_change(&state, active, desired, ms);
  if (changed)
    active = desired;
  return changed;
}

int main(void) {
  hbm_switch_reset(&state);
  // First change is immediate, even at startup; identical requests do nothing.
  assert(request(true, 0) && active);
  assert(!request(true, 50));
  // Bounce both ways inside the window without changing the display.
  assert(!request(false, 100) && active);
  assert(!request(true, 200) && active);
  assert(!request(false, 499) && active);
  // Returning to the original state leaves no delayed opposite change queued.
  assert(!request(true, 500) && active);
  // A fresh request after the window applies immediately, with no pose dwell.
  assert(request(false, 550) && !active);
  assert(!request(true, 600) && !active);
  assert(!request(false, 700) && !active);
  assert(!request(true, 1049) && !active);
  assert(!request(false, 1050) && !active);
  assert(request(true, 1100) && active);
  // A persistent opposite request is retried from the latest state at 500 ms;
  // suppressed samples do not restart the bounce window.
  for (uint64_t ms = 1150; ms < 1600; ms += 50)
    assert(!request(false, ms));
  assert(request(false, 1600) && !active);
  // Entering a new Stats session resets the switch guard.
  hbm_switch_reset(&state);
  assert(request(true, 1650) && active);
  puts("HBM switch bounce tests passed");
  return 0;
}
