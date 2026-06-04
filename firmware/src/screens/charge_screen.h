#ifndef __CHARGE_SCREEN_H
#define __CHARGE_SCREEN_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool is_charge_screen_active();
void setup_charge_properties();

#ifdef __cplusplus
}
#endif

#endif