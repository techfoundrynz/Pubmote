#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint64_t changed_ms;
  bool initialized;
} brightness_switch_t;

#ifdef __cplusplus
extern "C"
{
#endif

  void brightness_switch_reset(brightness_switch_t *state);
  // Accept the first change immediately, then ignore bouncing requests for 500 ms.
  // The caller supplies the actual display state and retries the latest request.
  bool brightness_switch_should_change(brightness_switch_t *state, uint16_t current, uint16_t requested,
                                       uint64_t now_ms);

#ifdef __cplusplus
}
#endif
