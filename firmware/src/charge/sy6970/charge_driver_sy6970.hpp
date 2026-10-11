#pragma once

#include <charge/charge_driver.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C"
{
#endif

  esp_err_t sy6970_charge_driver_init();
  RemotePowerState sy6970_get_power_state();
  bool sy6970_get_diagnostics(ChargeDiagnostics *diagnostics);
  void sy6970_disable_watchdog();
  void sy6970_enable_watchdog();
  void sy6970_charge_driver_deinit();
  esp_err_t sy6970_enter_protection_mode();

#ifdef __cplusplus
}
#endif
