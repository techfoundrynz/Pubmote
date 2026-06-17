#ifndef __GPIO_DETECTION_H
#define __GPIO_DETECTION_H
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#ifdef __cplusplus
extern "C" {
#endif


bool gpio_supports_wakeup_from_deep_sleep(gpio_num_t gpio_num);



#ifdef __cplusplus
}
#endif

#endif