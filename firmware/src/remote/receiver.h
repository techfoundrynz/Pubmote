#ifndef __RECEIVER_H
#define __RECEIVER_H

#include <stdbool.h>
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif


#define RSSI_NONE -100
#define RSSI_POOR -95
#define RSSI_FAIR -85
#define RSSI_GOOD -75

typedef enum {
  BOARD_STATE_BOOT,
  // Running
  BOARD_STATE_RUNNING,
  BOARD_STATE_RUNNING_TILTBACK,
  BOARD_STATE_RUNNING_WHEELSLIP,
  BOARD_STATE_RUNNING_UPSIDEDOWN,
  BOARD_STATE_RUNNING_FLYWHEEL,
  // Stopped
  BOARD_STATE_STOP_ANGLE_PITCH,
  BOARD_STATE_STOP_ANGLE_ROLL,
  BOARD_STATE_STOP_SWITCH_HALF,
  BOARD_STATE_STOP_SWITCH_FULL,

  BOARD_STATE_STARTUP = 11,
  BOARD_STATE_STOP_REVERSE,
  BOARD_STATE_STOP_QUICKSTOP,
  BOARD_STATE_CHARGING = 14,
  BOARD_STATE_DISABLED = 15,
} BoardState;

bool receiver_lock_channel();
void receiver_unlock_channel();
void receiver_init();
void receiver_deinit();



#ifdef __cplusplus
}
#endif

#endif