#include "utilities/number_utils.h"
#include <stdio.h>

float clampf(float value, float min, float max) {
  if (value < min)
    return min;
  if (value > max)
    return max;
  return value;
}

uint8_t clampu8(uint8_t value, uint8_t min, uint8_t max) {
  if (value < min)
    return min;
  if (value > max)
    return max;
  return value;
}