#pragma once
#include <stdint.h>

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

typedef enum {
  CHARGE_STATE_NOT_CHARGING,
  CHARGE_STATE_CHARGING,
  CHARGE_STATE_DONE,
  CHARGE_STATE_UNKNOWN,
} RemoteChargeState;

typedef enum {
  SWITCH_STATE_OFF,
  SWITCH_STATE_LEFT,
  SWITCH_STATE_RIGHT,
  SWITCH_STATE_BOTH
} SwitchState;

typedef struct {
  float speed; // canonical KPH
  uint8_t dutyCycle;
  float batteryVoltage;
  uint8_t batteryPercentage;
  float tripDistance;
  float motorTemp;
  float controllerTemp;
  BoardState state;
  SwitchState switchState;
} BoardTelemetry;
