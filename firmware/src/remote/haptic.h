#ifndef __HAPTIC_H
#define __HAPTIC_H
#include "haptic/haptic_driver.h"

void haptic_vibrate(HapticFeedbackPattern pattern);
void haptic_stop_vibration();
void haptic_init();
void haptic_deinit();

#endif