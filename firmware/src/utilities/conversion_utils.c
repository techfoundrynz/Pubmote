#include "conversion_utils.h"

float convert_ms_to_kph(float ms) {
  return ms * 3.6;
};

float convert_km_to_mi(float km) {
  return km * 0.621371;
};

// Same ratio, per unit time
float convert_kph_to_mph(float kph) {
  return convert_km_to_mi(kph);
};

float convert_m_to_ft(float m) {
  return m * 3.28084;
};

float convert_c_to_f(float c) {
  return c * 1.8 + 32;
};
