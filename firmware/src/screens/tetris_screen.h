#ifndef __TETRIS_SCREEN_H
#define __TETRIS_SCREEN_H
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool is_tetris_screen_active();
void setup_tetris_properties();
void teardown_tetris_properties();
uint32_t tetris_high_score();

#ifdef __cplusplus
}
#endif

#endif
