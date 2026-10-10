#include "remote/protocol.h"
#include "remote/settings_state.h"
#include <assert.h>
#include <string.h>

#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define MIN_RCV_API_VERSION 5
enum {
  COMMS_TYPE_ESPNOW,
  COMMS_TYPE_BLE
};
enum {
  PAIRING_STATE_UNPAIRED,
  PAIRING_STATE_PAIRING,
  PAIRING_STATE_PENDING,
  PAIRING_STATE_PAIRED
};
enum {
  CONNECTION_STATE_DISCONNECTED
};
typedef struct {
  uint8_t mac_addr[6];
  uint8_t chan;
} comms_event_t;
static int mode, pairing_state, sends, saves, connects, secret_notifications, completed, incompatible;
static uint32_t notified_secret;
static uint8_t sent[2], connected_channel;
static bool locked, can_lock = true, send_ok = true, auto_reconnect = true;
int comms_get_active_type(void) {
  return mode;
}
bool receiver_lock_channel(void) {
  locked = can_lock;
  return locked;
}
void receiver_unlock_channel(void) {
  assert(locked);
  locked = false;
}
void comms_set_channel(uint8_t channel) {
  assert(locked);
  connected_channel = channel;
}
void comms_connect_peer(const uint8_t *mac, uint8_t channel) {
  assert(locked && !memcmp(mac, settings_get_pairing().remote_addr, 6));
  connected_channel = channel;
}
esp_err_t comms_send(const uint8_t *mac, const uint8_t *data, size_t size) {
  assert(locked && size == 2 && !memcmp(mac, settings_get_pairing().remote_addr, 6));
  memcpy(sent, data, size);
  ++sends;
  return send_ok ? ESP_OK : ESP_FAIL;
}
int connection_get_pairing_state(void) {
  return pairing_state;
}
void connection_update_pairing_state(int state) {
  pairing_state = state;
}
void connection_set_auto_reconnect(bool enabled) {
  auto_reconnect = enabled;
}
void connection_update_state(int state) {
  assert(state == CONNECTION_STATE_DISCONNECTED);
}
void comms_disconnect_peer(const uint8_t *mac) {
  assert(!memcmp(mac, settings_get_pairing().remote_addr, 6));
}
void connection_connect_to_default_peer(void) {
  assert(pairing_state == PAIRING_STATE_PAIRED);
  ++connects;
}
esp_err_t save_pairing_data(void) {
  ++saves;
  return ESP_OK;
}
void pairing_ui_secret_received(uint32_t secret) {
  notified_secret = secret;
  ++secret_notifications;
}
void pairing_ui_completed(void) {
  ++completed;
}
void pairing_ui_incompatible_receiver(uint8_t version, uint8_t minimum) {
  assert(version == 1 && minimum == MIN_RCV_API_VERSION);
  ++incompatible;
}
#include "pairing/pairing.c"

int main(void) {
  SettingsSnapshot initial = {0};
  initial.pairing.default_index = -1;
  settings_state_init(&initial);
  comms_event_t event = {.mac_addr = {1, 2, 3, 4, 5, 6}, .chan = 6};
  uint8_t init[] = {1, 2, 3, 4, 5, 6, 11};
  assert(!pairing_process_init_event(init, 6, event));
  init[0] = 9;
  assert(!pairing_process_init_event(init, 7, event));
  init[0] = 1;
  can_lock = false;
  assert(!pairing_process_init_event(init, 7, event) && sends == 0);
  can_lock = true;
  send_ok = false;
  assert(!pairing_process_init_event(init, 7, event) && !locked && pairing_state == PAIRING_STATE_UNPAIRED);
  send_ok = true;
  assert(pairing_process_init_event(init, 7, event) && !locked && pairing_state == PAIRING_STATE_PAIRING);
  assert(connected_channel == 11 && sent[0] == REM_PAIR_BOND && sent[1] == 0);
  uint8_t secret[] = {0x81, 0x23, 0x45, 0x67};
  assert(!pairing_process_bond_event(secret, 3) && secret_notifications == 0);
  assert(pairing_process_bond_event(secret, 4));
  assert(pairing_state == PAIRING_STATE_PENDING && notified_secret == 0x81234567 && secret_notifications == 1);
  assert(!pairing_process_bond_event(secret, 4) && secret_notifications == 1);
  uint8_t accept = 1;
  assert(!pairing_process_completion_event(&accept, 2) && saves == 0);
  assert(pairing_process_completion_event(&accept, 1));
  assert(pairing_state == PAIRING_STATE_PAIRED && saves == 1 && connects == 1 && completed == 1);
  assert(!pairing_process_completion_event(&accept, 1) && saves == 1);
  mode = COMMS_TYPE_BLE;
  init[0] = 9;
  assert(pairing_process_init_event(init, 7, event) && connected_channel == 0x86);
  assert(pairing_process_bond_event(secret, 4));
  accept = 0;
  assert(!pairing_process_completion_event(&accept, 1) && pairing_state == PAIRING_STATE_UNPAIRED);
  assert(saves == 1 && completed == 1);
  handle_receiver_api_version_too_low(1);
  assert(!auto_reconnect && incompatible == 1);
  return 0;
}
