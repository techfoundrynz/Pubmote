#ifndef __CONNECTION_H
#define __CONNECTION_H
#include <esp_timer.h>


#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
  CONNECTION_STATE_DISCONNECTED,
  CONNECTION_STATE_CONNECTING,
  CONNECTION_STATE_CONNECTED,
  CONNECTION_STATE_RECONNECTING
} ConnectionState;

typedef enum {
  PAIRING_STATE_UNPAIRED,
  PAIRING_STATE_PAIRING,
  PAIRING_STATE_PENDING,
  PAIRING_STATE_PAIRED
} PairingState;

extern ConnectionState connection_state;
extern PairingState pairing_state;

void connection_update_state(ConnectionState state);
void connection_init();
void connection_deinit();
void connection_connect_to_peer(uint8_t *mac_addr, uint8_t channel);
void connection_connect_to_default_peer();



#ifdef __cplusplus
}
#endif

#endif