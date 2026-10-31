#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef enum {
    WAKE_SAMPLE_UNAVAILABLE = 0,
    WAKE_SAMPLE_PENDING = 1,
    WAKE_SAMPLE_CONFIRMED = 2,
    WAKE_SAMPLE_CANCELLED = 3,
  } WakeButtonSample;

  // Load before sleep, but leave the coprocessor stopped until a button wake.
  esp_err_t wake_button_sampler_prepare(int gpio, int active_level);
  // Preserve RTC input ownership until sampling finishes, before digital GPIO init.
  void wake_button_sampler_finish(void);
  WakeButtonSample wake_button_sampler_result(void);

#ifdef __cplusplus
}
#endif
