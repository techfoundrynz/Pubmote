#include "pairing.h"
#include "commands.h"
#include "connection.h"
#include "esp_log.h"
#include "comms.h"
#include "settings.h"
#include "remote/display.h"
#include "generated/app-window.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-PAIRING";

extern "C" bool pairing_process_init_event(uint8_t *data, int len, comms_event_t evt) {
  if (len == 6) {
    uint8_t rec_mac[COMMS_MAC_LEN];
    memcpy(rec_mac, data, COMMS_MAC_LEN);
    if (comms_get_active_type() == COMMS_TYPE_ESPNOW) {
      if (!comms_is_same_mac(evt.mac_addr, rec_mac)) {
        ESP_LOGE(TAG, "MAC Address mismatch on pairing request");
        return false;
      }
    }
    memcpy(pairing_settings.remote_addr, evt.mac_addr, COMMS_MAC_LEN);
    ESP_LOGI(TAG, "Got Pairing request from VESC Express");
    ESP_LOGI(TAG, "packet Length: %d", len);
    ESP_LOGI(TAG, "Sender MAC: %02X:%02X:%02X:%02X:%02X:%02X, Payload MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             evt.mac_addr[0], evt.mac_addr[1], evt.mac_addr[2], evt.mac_addr[3], evt.mac_addr[4], evt.mac_addr[5],
             data[0], data[1], data[2], data[3], data[4], data[5]);
    
    uint8_t PAIR_BOND_RES[2] = {REM_PAIR_BOND};
    if (comms_get_active_type() == COMMS_TYPE_BLE) {
      pairing_settings.channel = evt.chan | 0x80;
    } else {
      pairing_settings.channel = evt.chan;
    }
    
    uint8_t *mac_addr = pairing_settings.remote_addr;
    esp_err_t result = ESP_FAIL;

    if (receiver_lock_channel()) {
      comms_connect_peer(mac_addr, pairing_settings.channel);
      result = comms_send(mac_addr, (uint8_t *)&PAIR_BOND_RES, sizeof(PAIR_BOND_RES));
      receiver_unlock_channel();
    }

    if (result != ESP_OK) {
      ESP_LOGE(TAG, "Error sending pairing data: %d", result);
      return false;
    } else {
      ESP_LOGI(TAG, "Sent response back to VESC Express");
      pairing_state = PAIRING_STATE_PAIRING;
      return true;
    }
  } else {
    ESP_LOGE(TAG, "Invalid pairing init packet length: %d", len);
  }
  return false;
}

extern "C" bool pairing_process_bond_event(uint8_t *data, int len) {
  ESP_LOGI(TAG, "Pairing bond");
  if (pairing_state == PAIRING_STATE_PAIRING && len == 4) {
    ESP_LOGI(TAG, "Grabbing secret code");
    ESP_LOGI(TAG, "packet Length: %d", len);
    pairing_settings.secret_code = (int32_t)(data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    ESP_LOGI(TAG, "Secret Code: %li", pairing_settings.secret_code);
    
    char formattedString[32];
    snprintf(formattedString, sizeof(formattedString), "%ld", pairing_settings.secret_code);
    
    slint::SharedString pairing_code(formattedString);
    slint::invoke_from_event_loop([=]() {
      get_slint_window()->global<UiState>().set_pairing_code(pairing_code);
    });
    
    pairing_state = PAIRING_STATE_PENDING;
    return true;
  } else {
    ESP_LOGE(TAG, "Invalid pairing bond packet length: %d", len);
  }
  return false;
}

extern "C" bool pairing_process_completion_event(uint8_t *data, int len) {
  if (pairing_state == PAIRING_STATE_PENDING && len == 1) {
    ESP_LOGI(TAG, "Grabbing response");
    ESP_LOGI(TAG, "packet Length: %d", len);
    uint8_t response = data[0];
    ESP_LOGI(TAG, "Response: %i", response);

    if (response == 1) {
      pairing_state = PAIRING_STATE_PAIRED;
      save_pairing_data();
      connection_connect_to_default_peer();
      
      slint::invoke_from_event_loop([]() {
        get_slint_window()->global<UiState>().set_screen(Screen::Stats);
      });
      return true;
    } else {
      ESP_LOGI(TAG, "Pairing failed");
      pairing_state = PAIRING_STATE_UNPAIRED;
      return false;
    }
  } else {
    ESP_LOGE(TAG, "Invalid pairing complete packet length: %d", len);
  }
  return false;
}
