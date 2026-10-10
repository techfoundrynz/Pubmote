#pragma once
#include "colors.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef enum {
    UTILIZATION_STATUS_NONE,
    UTILIZATION_STATUS_CAUTION,
    UTILIZATION_STATUS_WARNING,
    UTILIZATION_STATUS_CRITICAL,
  } UtilizationStatus;

  typedef enum {
    UTILIZATION_THRESHOLD_CAUTION = 70,
    UTILIZATION_THRESHOLD_WARNING = 80,
    UTILIZATION_THRESHOLD_CRITICAL = 90,
  } UtilizationStatusThreshold;

  typedef enum {
    UTILIZATION_COLOR_NONE = 0,
    UTILIZATION_COLOR_CAUTION = LED_COLOR_CAUTION,
    UTILIZATION_COLOR_WARNING = LED_COLOR_WARNING,
    UTILIZATION_COLOR_CRITICAL = LED_COLOR_CRITICAL,
  } UtilizationStatusColor;

  UtilizationStatus get_utilization_status(uint8_t utilization);
  UtilizationStatusColor get_utilization_color(UtilizationStatus status);
  void vehicle_monitor_init();
  void vehicle_monitor_deinit();

#ifdef __cplusplus
}
#endif
