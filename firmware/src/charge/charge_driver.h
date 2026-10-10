#pragma once
#include "remote/telemetry_types.h"
#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct {
    uint16_t voltage;              // Battery voltage in mV
    RemoteChargeState chargeState; // Charging state
    uint16_t current;              // Charging current in mA
    bool isPowered;                // True if powered by VBUS
    bool isFault;
  } RemotePowerState;

  typedef struct {
    uint16_t voltage_mv; // Voltage in millivolts
    uint8_t percentage;  // Percentage of battery charge
  } VoltagePoint;

  // Generic interface for charge driver
  char *charge_state_to_string(RemoteChargeState state);
  esp_err_t charge_driver_init();
  RemotePowerState get_power_state();
  uint8_t battery_mv_to_percent(uint16_t voltage_mv);
  void disable_watchdog();
  void enable_watchdog();
  void enter_protection_mode();
  void charge_driver_deinit();

#ifdef __cplusplus
}
#endif
