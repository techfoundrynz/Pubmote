#ifndef __TRANSMITTER_H
#define __TRANSMITTER_H
#include <stdint.h>
#include <stdio.h>

#include "esp_now.h"


#ifdef __cplusplus
extern "C" {
#endif


#define FIRMWARE_ID "PUBREMOTE-0_0_1"

typedef struct {
  char *firmwareId;
} ParingInfo;

typedef struct {
  uint16_t size;
  uint16_t *results;
} LatencyTestResults;

void transmitter_init();
void transmitter_deinit();



#ifdef __cplusplus
}
#endif

#endif