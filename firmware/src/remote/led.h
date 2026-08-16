#ifndef __LED_H
#define __LED_H

#include <stdbool.h>
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} RGB;

// PULSE and RAINBOW are not user-selectable modes - they back the startup
// animation and the pairing screen respectively.
typedef enum {
  LED_EFFECT_NONE,
  LED_EFFECT_PULSE,
  LED_EFFECT_SOLID,
  LED_EFFECT_RAINBOW
} LedEffect;

// User-selectable LED behaviour, persisted in device_settings.led_mode and
// cycled from the main menu. Order is the cycle order shown to the user, and
// the values are persisted in NVS - append rather than renumber.
typedef enum {
  LED_MODE_OFF,    // Always dark, alerts included
  LED_MODE_SOLID,  // Solid theme colour
  LED_MODE_ALERTS, // Dark unless a duty alert is active
  LED_MODE_COUNT   // Sentinel - keep last
} LedModeOptions;

void led_init();
void led_deinit();
void led_set_brightness(uint8_t brightness);
void led_set_effect_solid(uint32_t color);
void led_set_effect_pulse(uint32_t color);
void led_set_effect_rainbow();
void led_set_effect_none();

// Apply the effect configured by device_settings.led_mode. Use this to return
// the LED to its resting behaviour after a temporary override (pairing,
// colour preview, duty alert).
void led_apply_mode();

// Short label for a mode, for display in the UI.
const char *led_mode_label(LedModeOptions mode);

// True when this board actually has an addressable LED.
bool led_is_supported();

// Temporary alert override, driven by the duty-cycle monitor. Suppressed in
// LED_MODE_OFF, which wants the LED dark at all times.
void led_set_alert(uint32_t color);
void led_clear_alert();


#ifdef __cplusplus
}
#endif

#endif
