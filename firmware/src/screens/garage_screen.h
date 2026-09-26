#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
bool is_garage_screen_active();
void setup_garage_properties();
void teardown_garage_properties();
void handle_garage_tick();
void handle_garage_action();
void handle_garage_steer(int direction);
void handle_garage_back();
uint32_t garage_high_score();
#ifdef __cplusplus
}
#endif
