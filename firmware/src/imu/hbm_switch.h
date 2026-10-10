#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint64_t changed_ms;
  bool initialized;
} hbm_switch_t;

#ifdef __cplusplus
extern "C" {
#endif

void hbm_switch_reset(hbm_switch_t *state);
// Accept the first change immediately, then ignore bouncing requests for 500 ms.
// The caller supplies the actual display state and retries the latest request.
bool hbm_switch_should_change(hbm_switch_t *state, bool current, bool requested, uint64_t now_ms);

#ifdef __cplusplus
}
#endif
