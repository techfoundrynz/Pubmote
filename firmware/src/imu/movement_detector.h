#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct {
    float baseline_x, baseline_y, baseline_z;
    uint64_t last_sample_ms, movement_since_ms, last_event_ms;
    bool initialized, moving, emitted;
  } movement_detector_t;

  void movement_detector_reset(movement_detector_t *state);
  // Acceleration is in g. Returns a debounced movement event, independent of pose.
  bool movement_detector_update(movement_detector_t *state, uint64_t now_ms, float ax, float ay, float az, bool valid);

#ifdef __cplusplus
}
#endif
