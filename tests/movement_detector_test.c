#include "imu/movement_detector.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
  movement_detector_t state;
  movement_detector_reset(&state);
  // Face-up on a charger, including small vibration, never emits movement.
  for (uint64_t t = 0; t <= 60000; t += 50)
    assert(!movement_detector_update(&state, t, (t % 100 ? 0.02f : -0.02f), 0, 1, true));
  // A pickup with unchanged orientation triggers after two samples.
  assert(!movement_detector_update(&state, 60050, 0.6f, 0, 1, true));
  assert(movement_detector_update(&state, 60100, 0.6f, 0, 1, true));
  assert(!movement_detector_update(&state, 60150, 0.6f, 0, 1, true));
  for (uint64_t t = 60200; t <= 64000; t += 50)
    assert(!movement_detector_update(&state, t, 0.6f, 0, 1, true));
  // Rotation is movement too; neither a particular face nor gravity magnitude is required.
  assert(!movement_detector_update(&state, 64050, 0, 1, 0, true));
  assert(movement_detector_update(&state, 64100, 0, 1, 0, true));
  // Fresh samples after an outage seed the baseline without waking.
  assert(!movement_detector_update(&state, 65000, 0, 0, -1, true));
  assert(!movement_detector_update(&state, 65050, 0, 0, -1, true));
  // One isolated spike and invalid samples cannot trigger wake.
  assert(!movement_detector_update(&state, 65100, 1, 0, -1, true));
  assert(!movement_detector_update(&state, 65150, 0, 0, -1, true));
  assert(!movement_detector_update(&state, 65200, NAN, 0, 0, true));
  assert(!movement_detector_update(&state, 65250, 1, 0, -1, false));
  assert(!movement_detector_update(&state, 66000, 0, 0, -1, true));
  assert(!movement_detector_update(&state, 66050, 0, 0, -0.4f, true));
  assert(!movement_detector_update(&state, 66100, 0, 0, -0.4f, false));
  assert(!movement_detector_update(&state, 66150, 0, 0, -0.4f, true));
  assert(movement_detector_update(&state, 66200, 0, 0, -0.4f, true));
  puts("movement detector tests passed");
}
