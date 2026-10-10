#include "imu/raise_detector.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static raise_detector_t state;
static uint64_t now;
static bool previous;
static unsigned transitions;

static bool sample(float x, float y, float z, bool valid, unsigned dt) {
  now += dt;
  bool raised = raise_detector_update(&state, now, x, y, z, valid);
  if (raised != previous) {
    previous = raised;
    ++transitions;
  }
  return raised;
}

static void reset(void) {
  raise_detector_reset(&state);
  now = 0;
  previous = false;
  transitions = 0;
}

static void pose(float z, unsigned ms) {
  float x = sqrtf(1.0f - z * z);
  for (unsigned t = 0; t < ms; t += 50)
    sample(x, 0, z, true, 50);
}

static void raise(void) {
  pose(0.0f, 1500);
  pose(0.9f, 1000);
  assert(state.raised);
}

int main(void) {
  // Booting on a table never flashes HBM.
  reset();
  pose(1.0f, 20000);
  assert(!state.raised && transitions == 0);

  // Raise, hold still beyond the old 3-second table cutoff, then lower.
  reset();
  raise();
  pose(0.9f, 5000);
  assert(state.raised);
  pose(0.0f, 1000);
  assert(!state.raised && transitions == 2);

  // Hysteresis keeps the viewing state despite jitter around the old cutoff.
  reset();
  raise();
  for (unsigned i = 0; i < 100; ++i)
    pose(i % 2 ? 0.69f : 0.71f, 50);
  assert(state.raised && transitions == 1);

  // Pose detection has no dwell timer. Switch bounce is tested separately.
  reset();
  pose(0, 100);
  pose(0.9f, 50);
  assert(state.raised && transitions == 1);
  pose(0, 50);
  assert(!state.raised && transitions == 2);
  pose(0.9f, 50);
  assert(state.raised && transitions == 3);

  // A same-orientation pickup with acceleration can arm the detector.
  reset();
  pose(1.0f, 1500);
  sample(0, 0, 1.19f, true, 50);
  pose(1.0f, 1000);
  assert(state.raised);

  // The bounded reading window expires and a stationary pose cannot retrigger.
  pose(1.0f, 20000);
  assert(!state.raised && transitions == 2);
  pose(1.0f, 5000);
  assert(!state.raised && transitions == 2);

  // Invalid, NaN and shock samples cannot enable HBM.
  reset();
  pose(0, 1500);
  for (unsigned i = 0; i < 30; ++i)
    sample(0, 0, 1, false, 50);
  for (unsigned i = 0; i < 30; ++i)
    sample(NAN, 0, 1, true, 50);
  for (unsigned i = 0; i < 30; ++i)
    sample(0, 0, 3, true, 50);
  pose(1, 1000);
  assert(!state.raised);

  // Short sensor outages keep HBM steady; sustained outages turn it off.
  reset();
  raise();
  for (unsigned i = 0; i < 4; ++i)
    sample(0, 0, 0, false, 50);
  assert(state.raised);
  pose(0.9f, 500);
  assert(state.raised);
  for (unsigned i = 0; i < 15; ++i)
    sample(0, 0, 0, false, 50);
  assert(!state.raised);

  // A gap does not fabricate pickup motion from a static face-up sample.
  reset();
  pose(0, 1000);
  sample(0, 0, 1, true, 2000);
  pose(1, 1000);
  assert(!state.raised);
  puts("raise detector tests passed");
  return 0;
}
