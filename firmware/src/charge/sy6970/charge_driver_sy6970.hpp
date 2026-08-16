#ifndef __CHARGE_DRIVER_SY6970_HPP
#define __CHARGE_DRIVER_SY6970_HPP

#include <esp_err.h>
#include <charge/charge_driver.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sy6970_charge_driver_init();
RemotePowerState sy6970_get_power_state();
void sy6970_disable_watchdog();
void sy6970_enable_watchdog();
void sy6970_charge_driver_deinit();
esp_err_t sy6970_enter_protection_mode();

#ifdef __cplusplus
}
#endif

#endif