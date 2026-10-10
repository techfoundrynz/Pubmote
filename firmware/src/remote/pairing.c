#include "pairing.h"
#include "config.h"
#include "connection.h"
#include "esp_log.h"
#include "remote/protocol.h"
#include "remote/receiver.h"
#include "remote/settings_snapshot.h"
#include "remote/settings_state.h"
#include "settings.h"
#include "ui/pairing_feedback.h"

static const char *TAG = "PUBREMOTE-PAIRING";

bool pairing_process_init_event(uint8_t *data, int len, comms_event_t evt) {
  bool ble = comms_get_active_type() == COMMS_TYPE_BLE;
  uint8_t channel;
  if (len < 0 || !protocol_decode_pair_init(data, (size_t)len, evt.mac_addr, ble, evt.chan, &channel)) {
    ESP_LOGE(TAG, "Invalid pairing init packet (length or sender MAC)");
    return false;
  }
  ESP_LOGI(TAG, "Got Pairing request from VESC Express");
  if (!ble)
    ESP_LOGI(TAG, "Board reports channel %d (heard on %d)", data[6], evt.chan);
  settings_set_peer(evt.mac_addr, channel);
  uint8_t response[PROTOCOL_PAIR_RESPONSE_BYTES];
  size_t size = protocol_encode_pair_response(response, sizeof(response));
  esp_err_t result = ESP_FAIL;
  if (receiver_lock_channel()) {
    if (!ble)
      comms_set_channel(channel);
    comms_connect_peer(evt.mac_addr, channel);
    ESP_LOGI(TAG, "Sending PAIR_BOND response on channel %d", channel);
    result = comms_send(evt.mac_addr, response, size);
    receiver_unlock_channel();
  }
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "Error sending pairing data: %d", result);
    return false;
  }
  connection_update_pairing_state(PAIRING_STATE_PAIRING);
  return true;
}
bool pairing_process_bond_event(uint8_t *data, int len) {
  uint32_t secret;
  if (connection_get_pairing_state() != PAIRING_STATE_PAIRING || len < 0 ||
      !protocol_decode_pair_secret(data, (size_t)len, &secret))
    return false;
  settings_set_pairing_secret(secret);
  ESP_LOGI(TAG, "Code received; waiting for pairing confirmation");
  pairing_ui_secret_received(secret);
  connection_update_pairing_state(PAIRING_STATE_PENDING);
  return true;
}
bool pairing_process_completion_event(uint8_t *data, int len) {
  bool accepted;
  if (connection_get_pairing_state() != PAIRING_STATE_PENDING || len < 0 ||
      !protocol_decode_pair_complete(data, (size_t)len, &accepted))
    return false;
  if (!accepted) {
    ESP_LOGI(TAG, "Pairing failed");
    connection_update_pairing_state(PAIRING_STATE_UNPAIRED);
    return false;
  }
  connection_update_pairing_state(PAIRING_STATE_PAIRED);
  ESP_LOGI(TAG, "Pairing confirmed");
  save_pairing_data();
  connection_connect_to_default_peer();
  pairing_ui_completed();
  return true;
}
void handle_receiver_api_version_too_low(uint8_t version) {
  ESP_LOGW(TAG, "Receiver API version too low: %d. Disconnecting.", version);
  connection_set_auto_reconnect(false);
  connection_update_state(CONNECTION_STATE_DISCONNECTED);
  PairingSettings peer = settings_get_pairing();
  comms_disconnect_peer(peer.remote_addr);
  pairing_ui_incompatible_receiver(version, MIN_RCV_API_VERSION);
}
