#ifndef __REMOTEINPUTS_H
#define __REMOTEINPUTS_H
#include "adc.h"
#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
  BUTTON_EVENT_DOWN,
  BUTTON_EVENT_UP,
  BUTTON_EVENT_PRESS,
  BUTTON_EVENT_DOUBLE_PRESS,
  BUTTON_EVENT_LONG_PRESS_HOLD,
} ButtonEvent;

typedef enum {
  BUTTON_PRIMARY
} ButtonType;

typedef bool (*button_callback_t)(void);

typedef struct {
  float js_y;
  float js_x;
  bool bt_c;
  bool bt_z;
  bool is_rev;
} RemoteData;

typedef struct {
  uint16_t x;
  uint16_t y;
} JoystickData;

extern RemoteData remote_data;
extern JoystickData joystick_data;

void thumbstick_init();
void buttons_init();
void buttons_deinit();
void register_primary_button_cb(ButtonEvent event, button_callback_t cb);
void unregister_primary_button_cb(ButtonEvent event);
float convert_adc_to_axis(int adc_value, int min_val, int mid_val, int max_val, int deadband, float expo, bool invert);



#ifdef __cplusplus
}
#endif

#endif