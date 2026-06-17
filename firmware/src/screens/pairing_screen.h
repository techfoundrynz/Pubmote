#ifndef __PAIRING_SCREEN_H
#define __PAIRING_SCREEN_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool is_pairing_screen_active();
void setup_pairing_properties();
void handle_pairing_action();

#ifdef __cplusplus
}
#endif

#endif