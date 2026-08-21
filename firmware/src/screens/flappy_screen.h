#ifndef __FLAPPY_SCREEN_H
#define __FLAPPY_SCREEN_H
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool is_flappy_screen_active();
void setup_flappy_properties();
void teardown_flappy_properties();
uint32_t flappy_high_score();

#ifdef __cplusplus
}
#endif

#endif
