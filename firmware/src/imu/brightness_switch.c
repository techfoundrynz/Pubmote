#include "brightness_switch.h"

#define BRIGHTNESS_SWITCH_DEBOUNCE_MS 500

void brightness_switch_reset(brightness_switch_t *state) {
  state->initialized = false;
  state->changed_ms = 0;
}

bool brightness_switch_should_change(brightness_switch_t *state, uint16_t current, uint16_t requested,
                                     uint64_t now_ms) {
  if (current == requested)
    return false;
  if (state->initialized && now_ms - state->changed_ms < BRIGHTNESS_SWITCH_DEBOUNCE_MS)
    return false;
  state->initialized = true;
  state->changed_ms = now_ms;
  return true;
}
