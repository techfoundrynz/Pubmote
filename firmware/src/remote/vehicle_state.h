#ifndef __VEHICLE_STATE_H
#define __VEHICLE_STATE_H
#include "colors.h"
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
  DUTY_STATUS_NONE,
  DUTY_STATUS_CAUTION,
  DUTY_STATUS_WARNING,
  DUTY_STATUS_CRITICAL,
} DutyStatus;

typedef enum {
  DUTY_THRESHOLD_CAUTION = 70,
  DUTY_THRESHOLD_WARNING = 80,
  DUTY_THRESHOLD_CRITICAL = 90,
} DutyStatusThreshold;

typedef enum {
  DUTY_COLOR_NONE = 0,
  DUTY_COLOR_CAUTION = COLOR_CAUTION,
  DUTY_COLOR_WARNING = COLOR_WARNING,
  DUTY_COLOR_CRITICAL = COLOR_CRITICAL,
} DutyStatusColor;

DutyStatus get_duty_status(uint8_t duty);
DutyStatusColor get_duty_color(DutyStatus status);
void vehicle_monitor_init();
void vehicle_monitor_deinit();



#ifdef __cplusplus
}
#endif

#endif