#ifndef __BUZZER_H
#define __BUZZER_H

#include "tones.h"
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
  BUZZER_PATTERN_NONE,
  BUZZER_PATTERN_MELODY,
  BUZZER_PATTERN_SOLID,
} BuzzerPatttern;

void buzzer_init();
void buzzer_deinit();
void buzzer_set_pattern(BuzzerPatttern pattern);
void buzzer_set_tone(BuzzerToneFrequency note, int duration);
void buzzer_stop();



#ifdef __cplusplus
}
#endif

#endif