#ifndef __LED_H
#define __LED_H

#include <stdio.h>

typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} RGB;

typedef enum {
  LED_EFFECT_NONE,
  LED_EFFECT_PULSE,
  LED_EFFECT_SOLID,
  LED_EFFECT_RAINBOW
} LedEffect;

void led_init();
void led_deinit();
void led_set_brightness(uint8_t brightness);
void led_set_effect_solid(uint32_t color);
void led_set_effect_pulse(uint32_t color);
void led_set_effect_rainbow();
void led_set_effect_none();
void led_set_effect_default();

#endif