#ifndef __CONNECTION_H
#define __CONNECTION_H
#include "comms.h"
#include <esp_timer.h>
#include <stdbool.h>
#include <stdint.h>


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
esp_err_t connection_switch_comms_mode(CommsType type);

// Enable/disable automatic reconnection attempts while disconnected.
// Disabled by an explicit user disconnect; re-enabled by any connect.
void connection_set_auto_reconnect(bool enabled);
bool connection_get_auto_reconnect();

// Re-derive pairing_state (and the active peer fields) from the saved paired
// devices. Used when leaving the pairing screen, which force-resets the state.
void connection_refresh_pairing_state();



#ifdef __cplusplus
}
#endif

#endif