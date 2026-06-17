#ifndef __TONES_H
#define __TONES_H


#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
  NOTE_C4 = 261,
  NOTE_D4 = 294,
  NOTE_E4 = 329,
  NOTE_F4 = 349,
  NOTE_G4 = 392,
  NOTE_A4 = 440,
  NOTE_B4 = 493,
  NOTE_C5 = 523,
  NOTE_SUCCESS = 532,
  NOTE_ERROR = 187,
  NOTE_CAUTION = 349,
  NOTE_WARNING = 440,
  NOTE_CRITICAL = 523,
} BuzzerToneFrequency;



#ifdef __cplusplus
}
#endif

#endif