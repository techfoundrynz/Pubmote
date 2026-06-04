
#ifndef __CONVERSION_UTILS_H
#define __CONVERSION_UTILS_H


#ifdef __cplusplus
extern "C" {
#endif


#define MILES_LABEL "MI"
#define KILOMETERS_LABEL "KM"
#define MILES_PER_HOUR_LABEL "MPH"
#define KILOMETERS_PER_HOUR_LABEL "KPH"
#define FAHRENHEIT_LABEL "F"
#define CELSIUS_LABEL "C"

float convert_ms_to_kph(float ms);
float convert_kph_to_mph(float kph);

float convert_m_to_ft(float m);

float convert_c_to_f(float c);



#ifdef __cplusplus
}
#endif

#endif