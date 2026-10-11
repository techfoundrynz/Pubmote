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
    const char *chip;
    // -1 means a register could not be read. REG0C is sampled by the power
    // task only, because reading it acknowledges latched charger faults.
    int16_t registers[0x15];
    int16_t fault_latched;
    int16_t fault_current;
    uint32_t fault_sample_ms;
    uint32_t sample_ms;
    int16_t last_fault;
    uint32_t last_fault_ms;
  } ChargeDiagnostics;

  typedef struct {
    uint16_t voltage_mv; // Voltage in millivolts
    uint8_t percentage;  // Percentage of battery charge
  } VoltagePoint;

  // Generic interface for charge driver
  char *charge_state_to_string(RemoteChargeState state);
  esp_err_t charge_driver_init();
  RemotePowerState get_power_state();
  // Return the latest cached sample and request a refresh on the power task.
  // A supported board returns sample_ms == 0 while the first read is pending.
  bool charge_driver_get_diagnostics(ChargeDiagnostics *diagnostics);
  uint8_t battery_mv_to_percent(uint16_t voltage_mv);
  void disable_watchdog();
  void enable_watchdog();
  void enter_protection_mode();
  bool charge_driver_supports_shipping_mode();
  esp_err_t charge_driver_enter_shipping_mode();
  void charge_driver_deinit();

#ifdef __cplusplus
}
#endif
