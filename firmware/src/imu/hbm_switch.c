#include "hbm_switch.h"

#define HBM_SWITCH_DEBOUNCE_MS 500

void hbm_switch_reset(hbm_switch_t *state) {
  state->initialized = false;
  state->changed_ms = 0;
}

bool hbm_switch_should_change(hbm_switch_t *state, bool current, bool requested, uint64_t now_ms) {
  if (current == requested)
    return false;
  if (state->initialized && now_ms - state->changed_ms < HBM_SWITCH_DEBOUNCE_MS)
    return false;
  state->initialized = true;
  state->changed_ms = now_ms;
  return true;
}
