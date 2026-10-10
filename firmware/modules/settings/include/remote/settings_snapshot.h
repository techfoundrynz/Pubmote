#pragma once
#include "remote/settings_types.h"
#ifdef __cplusplus
extern "C"
{
#endif
  typedef struct {
    DeviceSettings device;
    InputPinSettings pins;
    CalibrationSettings calibration;
    ImuCalibrationSettings imu;
    PairingSettings pairing;
  } SettingsSnapshot;

  SettingsSnapshot settings_snapshot(void);
  DeviceSettings settings_get_device(void);
  InputPinSettings settings_get_pins(void);
  CalibrationSettings settings_get_calibration(void);
  ImuCalibrationSettings settings_get_imu(void);
  PairingSettings settings_get_pairing(void);
#ifdef __cplusplus
}
#endif
