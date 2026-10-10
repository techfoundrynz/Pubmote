#pragma once
#include <stdbool.h>
#include <stdint.h>

// Accelerometer-only raise-to-view state. Times are monotonic milliseconds.
typedef struct {
  float gravity_x, gravity_y, gravity_z;
  uint64_t last_sample_ms, raised_ms;
  uint64_t armed_ms;
  bool initialized, raised, armed;
} raise_detector_t;

void raise_detector_reset(raise_detector_t *state);
bool raise_detector_update(raise_detector_t *state, uint64_t now_ms, float ax, float ay, float az, bool valid);
