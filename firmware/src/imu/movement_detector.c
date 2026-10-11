#include "movement_detector.h"
#include <math.h>
#include <string.h>

void movement_detector_reset(movement_detector_t *state) {
  memset(state, 0, sizeof(*state));
}

bool movement_detector_update(movement_detector_t *state, uint64_t now_ms, float ax, float ay, float az, bool valid) {
  if (!valid || !isfinite(ax) || !isfinite(ay) || !isfinite(az)) {
    state->moving = false;
    return false;
  }
  if (!state->initialized || now_ms - state->last_sample_ms > 250) {
    state->baseline_x = ax;
    state->baseline_y = ay;
    state->baseline_z = az;
    state->initialized = true;
    state->moving = false;
    state->last_sample_ms = now_ms;
    return false;
  }
  const float dx = ax - state->baseline_x;
  const float dy = ay - state->baseline_y;
  const float dz = az - state->baseline_z;
  const float dt = (float)(now_ms - state->last_sample_ms);
  const float alpha = dt / (300.0f + dt);
  state->baseline_x += alpha * dx;
  state->baseline_y += alpha * dy;
  state->baseline_z += alpha * dz;
  state->last_sample_ms = now_ms;

  // Require >0.2 g change across at least two normal 50 ms samples. A resting
  // orientation, sensor noise, or a single isolated spike must not wake the UI.
  if (dx * dx + dy * dy + dz * dz < 0.2f * 0.2f) {
    state->moving = false;
    return false;
  }
  if (!state->moving) {
    state->moving = true;
    state->movement_since_ms = now_ms;
    return false;
  }
  if (now_ms - state->movement_since_ms < 40 || (state->emitted && now_ms - state->last_event_ms < 750))
    return false;
  state->emitted = true;
  state->last_event_ms = now_ms;
  return true;
}
