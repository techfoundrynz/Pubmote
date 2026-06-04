#ifndef __STATS_SCREEN_H
#define __STATS_SCREEN_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool is_stats_screen_active();
void setup_stats_properties();
void teardown_stats_properties();

#ifdef __cplusplus
}
#endif

#endif