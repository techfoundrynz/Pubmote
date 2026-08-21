#ifndef __BUZZER_H
#define __BUZZER_H

#include "tones.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
  BUZZER_PATTERN_NONE,
  BUZZER_PATTERN_MELODY,
  BUZZER_PATTERN_SOLID,
} BuzzerPatttern;

// One step of a sequence. frequency NOTE_REST is a silence of the same length.
// duration_ms covers the whole step; a short articulation gap is taken from the
// end of it so repeated notes are distinguishable.
typedef struct {
  uint16_t frequency;
  uint16_t duration_ms;
} BuzzerNote;

void buzzer_init();
void buzzer_deinit();

// Plays notes in order, optionally looping. The array must outlive playback -
// pass a static one. buzzer_stop() ends it.
void buzzer_play_sequence(const BuzzerNote *notes, size_t count, bool repeat);
bool buzzer_sequence_playing();
void buzzer_set_pattern(BuzzerPatttern pattern);
void buzzer_set_tone(BuzzerToneFrequency note, int duration);
void buzzer_stop();



#ifdef __cplusplus
}
#endif

#endif