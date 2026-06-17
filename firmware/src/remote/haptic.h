#ifndef __HAPTIC_H
#define __HAPTIC_H
#include "haptic/haptic_driver.h"


#ifdef __cplusplus
extern "C" {
#endif


void haptic_vibrate(HapticFeedbackPattern pattern);
void haptic_stop_vibration();
void haptic_init();
void haptic_deinit();



#ifdef __cplusplus
}
#endif

#endif