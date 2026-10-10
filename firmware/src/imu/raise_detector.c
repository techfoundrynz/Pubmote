#include "raise_detector.h"
#include <math.h>
#include <string.h>

// Starting values to tune with recorded pickup, reading and riding traces.
#define FILTER_MS 150.0f
#define ENTER_Z 0.75f
#define EXIT_Z 0.55f
#define PICKUP_RESIDUAL_G 0.12f
#define MIN_ACCEL_G 0.8f
#define MAX_ACCEL_G 1.2f
#define MAX_RAISED_MS 15000
#define ARM_MS 2500
#define SAMPLE_GAP_MS 250
#define INVALID_MS 500

void raise_detector_reset(raise_detector_t *state) {
  memset(state, 0, sizeof(*state));
}

static bool set_lowered(raise_detector_t *state) {
  if (state->raised) {
    state->raised = false;
    state->armed = false;
  }
  return state->raised;
}

bool raise_detector_update(raise_detector_t *state, uint64_t now_ms, float ax, float ay, float az, bool valid) {
  if (state->raised && now_ms - state->raised_ms >= MAX_RAISED_MS) {
    return set_lowered(state);
  }
  float magnitude = sqrtf(ax * ax + ay * ay + az * az);
  valid = valid && isfinite(magnitude) && magnitude >= MIN_ACCEL_G && magnitude <= MAX_ACCEL_G;
  if (!valid) {
    // Missing/implausible samples cannot establish a pose or a pickup.
    state->armed = false;
    if (state->initialized && now_ms - state->last_sample_ms >= INVALID_MS) {
      return set_lowered(state);
    }
    return state->raised;
  }

  bool fresh = !state->initialized || now_ms - state->last_sample_ms > SAMPLE_GAP_MS;
  float residual = 0.0f;
  if (fresh) {
    state->gravity_x = ax;
    state->gravity_y = ay;
    state->gravity_z = az;
    state->armed = false;
    state->initialized = true;
  }
  else {
    float dx = ax - state->gravity_x;
    float dy = ay - state->gravity_y;
    float dz = az - state->gravity_z;
    residual = sqrtf(dx * dx + dy * dy + dz * dz);
    float dt = (float)(now_ms - state->last_sample_ms);
    float alpha = dt / (FILTER_MS + dt);
    state->gravity_x += alpha * dx;
    state->gravity_y += alpha * dy;
    state->gravity_z += alpha * dz;
  }
  state->last_sample_ms = now_ms;
  // Pose transitions are immediate. Bounce suppression belongs to the HBM
  // switch, rather than adding a dwell requirement to the pose detector.
  // The gravity filter above is used only to recognize pickup motion.
  float z = az / magnitude;

  if (!state->raised) {
    // A static face-up remote at startup must not enable HBM. Allow a lowered
    // pose or pickup motion, including a pickup that retains its orientation.
    if (z < EXIT_Z || (!fresh && residual > PICKUP_RESIDUAL_G)) {
      state->armed = true;
      state->armed_ms = now_ms;
    }
    if (state->armed && now_ms - state->armed_ms > ARM_MS) {
      state->armed = false;
    }
  }
  if (state->raised) {
    if (z < EXIT_Z)
      return set_lowered(state);
  }
  else if (state->armed && z > ENTER_Z) {
    state->raised = true;
    state->raised_ms = now_ms;
    state->armed = false;
  }
  return state->raised;
}
