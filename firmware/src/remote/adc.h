#ifndef __ADC_H
#define __ADC_H
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_oneshot.h>
#include <math.h>


#ifdef __cplusplus
extern "C" {
#endif


#define STICK_ADC_BITWIDTH ADC_BITWIDTH_12
#define STICK_MAX_VAL ((1 << STICK_ADC_BITWIDTH) - 1)
#define STICK_MID_VAL ((STICK_MAX_VAL / 2) - 1)
#define STICK_MIN_VAL 0
#define STICK_DEADBAND 10
#define STICK_EXPO 1
#define INVERT_Y_AXIS false

extern const adc_oneshot_chan_cfg_t adc_channel_config;

extern adc_oneshot_unit_handle_t adc1_handle;
extern adc_cali_handle_t adc1_cali_handle;

extern adc_oneshot_unit_handle_t adc2_handle;
extern adc_cali_handle_t adc2_cali_handle;

void init_adcs();



#ifdef __cplusplus
}
#endif

#endif