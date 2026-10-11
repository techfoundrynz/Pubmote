#pragma once
#include "remote/settings_types.h"
#include <stdbool.h>
#include <stdint.h>

// Temporary Stats-screen brightness; never changes the saved preference.
static inline uint8_t pose_brightness_level(DimmingModeOptions mode, uint8_t normal, bool raised, bool pocket,
                                            bool dialog, bool imu_supported) {
  if (!imu_supported || mode == DIMMING_MODE_OFF || mode == DIMMING_MODE_HBM || mode == DIMMING_MODE_HBM_ALWAYS ||
      raised || pocket || dialog)
    return normal;
  return mode == DIMMING_MODE_SCREEN_OFF ? 0 : normal / 2;
}

static inline bool pose_brightness_hbm(DimmingModeOptions mode, bool raised, bool pocket, bool dialog,
                                       bool imu_supported, bool hbm_supported) {
  if (mode == DIMMING_MODE_HBM_ALWAYS)
    return hbm_supported;
  return mode == DIMMING_MODE_HBM && raised && !pocket && !dialog && imu_supported && hbm_supported;
}

static inline bool pose_brightness_mode_supported(DimmingModeOptions mode, bool imu_supported, bool hbm_supported) {
  switch (mode) {
  case DIMMING_MODE_OFF:
    return true;
  case DIMMING_MODE_DIM:
  case DIMMING_MODE_SCREEN_OFF:
    return imu_supported;
  case DIMMING_MODE_HBM:
    return imu_supported && hbm_supported;
  case DIMMING_MODE_HBM_ALWAYS:
    return hbm_supported;
  default:
    return false;
  }
}
