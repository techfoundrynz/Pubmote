#ifndef __CHARGE_DRIVER_AXP2101_HPP
#define __CHARGE_DRIVER_AXP2101_HPP

#include <esp_err.h>
#include <charge/charge_driver.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t axp2101_charge_driver_init();
void axp2101_charge_driver_deinit();
RemotePowerState axp2101_get_power_state();
void axp2101_disable_watchdog();
void axp2101_enable_watchdog();

#ifdef __cplusplus
}
#endif

#endif