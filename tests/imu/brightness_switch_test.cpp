#include "imu/brightness_switch.h"
#include "imu/pose_brightness.h"
#include <assert.h>
#include <initializer_list>
#include <stdio.h>

static brightness_switch_t state;
static bool active;
static bool request(bool desired, uint64_t ms) {
  bool changed = brightness_switch_should_change(&state, active, desired, ms);
  if (changed)
    active = desired;
  return changed;
}

int main(void) {
  brightness_switch_reset(&state);
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
  brightness_switch_reset(&state);
  assert(request(true, 1650) && active);
  for (unsigned level : {0u, 1u, 10u, 101u, 255u}) {
    assert(pose_brightness_level(DIMMING_MODE_OFF, level, false, false, false, true) == level);
    assert(pose_brightness_level(DIMMING_MODE_DIM, level, true, false, false, true) == level);
    assert(pose_brightness_level(DIMMING_MODE_DIM, level, false, false, false, true) == level / 2);
    assert(pose_brightness_level(DIMMING_MODE_SCREEN_OFF, level, false, false, false, true) == 0);
    assert(pose_brightness_level(DIMMING_MODE_SCREEN_OFF, level, true, false, false, true) == level);
    assert(pose_brightness_level(DIMMING_MODE_SCREEN_OFF, level, false, true, false, true) == level);
    assert(pose_brightness_level(DIMMING_MODE_SCREEN_OFF, level, false, false, true, true) == level);
    assert(pose_brightness_level(DIMMING_MODE_SCREEN_OFF, level, false, false, false, false) == level);
    assert(pose_brightness_level(DIMMING_MODE_HBM, level, false, false, false, true) == level);
    assert(pose_brightness_level(DIMMING_MODE_HBM_ALWAYS, level, false, false, false, false) == level);
  }
  assert(pose_brightness_hbm(DIMMING_MODE_HBM, true, false, false, true, true));
  assert(!pose_brightness_hbm(DIMMING_MODE_HBM, false, false, false, true, true));
  assert(!pose_brightness_hbm(DIMMING_MODE_HBM, true, false, false, false, true));
  assert(!pose_brightness_hbm(DIMMING_MODE_HBM, true, false, false, true, false));
  assert(pose_brightness_hbm(DIMMING_MODE_HBM_ALWAYS, false, false, false, false, true));
  assert(!pose_brightness_hbm(DIMMING_MODE_HBM_ALWAYS, false, false, false, true, false));
  assert(!pose_brightness_mode_supported(DIMMING_MODE_DIM, false, true));
  assert(pose_brightness_mode_supported(DIMMING_MODE_DIM, true, false));
  assert(pose_brightness_mode_supported(DIMMING_MODE_HBM_ALWAYS, false, true));
  assert(!pose_brightness_mode_supported(DIMMING_MODE_HBM, false, true));
  brightness_switch_reset(&state);
  assert(brightness_switch_should_change(&state, 100, 50, 0));
  assert(!brightness_switch_should_change(&state, 50, 100, 499));
  assert(brightness_switch_should_change(&state, 50, 100, 500));
  puts("Brightness switch and pose dimming tests passed");
  return 0;
}
