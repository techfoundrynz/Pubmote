#pragma once

#include "esp_err.h"
#include "remote/telemetry_types.h"
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define RSSI_NONE -100
#define RSSI_POOR -95
#define RSSI_FAIR -85
#define RSSI_GOOD -75

  bool receiver_lock_channel();
  void receiver_unlock_channel();
  void receiver_init();
  esp_err_t receiver_start(void);
  esp_err_t receiver_deinit(void);

#ifdef __cplusplus
}
#endif
