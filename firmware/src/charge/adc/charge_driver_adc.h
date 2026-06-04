#ifndef __CHARGE_DRIVER_ADC_H
#define __CHARGE_DRIVER_ADC_H
#include <charge/charge_driver.h>
#include <esp_err.h>


#ifdef __cplusplus
extern "C" {
#endif


#ifndef BAT_ADC // Note: must be ADC1
  #define BAT_ADC -1
#endif
#ifndef BAT_ADC_F
  #define BAT_ADC_F 0
#endif

esp_err_t adc_charge_driver_init();
RemotePowerState adc_get_power_state();



#ifdef __cplusplus
}
#endif

#endif