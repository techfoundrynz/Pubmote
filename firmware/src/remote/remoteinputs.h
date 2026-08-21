#ifndef __REMOTEINPUTS_H
#define __REMOTEINPUTS_H
#include "adc.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif


// Defined in settings.h
struct InputPinSettings;

// Clicks are routed through input_router.h so there is one place a screen can
// claim them. These are the raw slots power management reserves for wake and
// shutdown; screens must not take them.
typedef enum {
  BUTTON_EVENT_DOWN,
  BUTTON_EVENT_UP,
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

// ─── Runtime input pin mapping ───────────────────────────────────────────────
bool input_pins_x_enabled();
bool input_pins_y_enabled();
bool input_pins_joystick_enabled();
bool input_pins_button_enabled();

// Returns ESP_OK if usable, otherwise fills err with a user-facing reason
esp_err_t input_pins_validate(const struct InputPinSettings *cfg, char *err, size_t err_len);

// Non-fatal notes about an assignment; returns characters written
size_t input_pins_warnings(const struct InputPinSettings *cfg, char *warn, size_t warn_len);

// Validate, persist and apply live: inputs are rebuilt, no reboot needed
esp_err_t input_pins_apply(const struct InputPinSettings *cfg, char *err, size_t err_len);

// GPIOs usable as an analog (joystick) input
uint64_t input_pins_adc_capable_mask();

// GPIOs this board already uses
uint64_t input_pins_reserved_mask();

// GPIOs that exist on this chip and aren't taken by the board
uint64_t input_pins_assignable_mask();

// GPIOs usable for the button: RTC pads only, since ext1 is the wake source
uint64_t input_pins_button_capable_mask();



#ifdef __cplusplus
}
#endif

#endif