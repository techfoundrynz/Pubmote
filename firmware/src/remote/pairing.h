#ifndef __PAIRING_H
#define __PAIRING_H

#include "comms.h"
#include <stdbool.h>
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif


bool pairing_process_init_event(uint8_t *data, int len, comms_event_t evt);
bool pairing_process_bond_event(uint8_t *data, int len);
bool pairing_process_completion_event(uint8_t *data, int len);
void handle_receiver_api_version_too_low(uint8_t api_version);



#ifdef __cplusplus
}
#endif

#endif