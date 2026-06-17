#ifndef __NUMBER_UTILS_H
#define __NUMBER_UTILS_H
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif


float clampf(float val, float min, float max);
uint8_t clampu8(uint8_t val, uint8_t min, uint8_t max);



#ifdef __cplusplus
}
#endif

#endif