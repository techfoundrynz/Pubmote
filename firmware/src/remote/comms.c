#include "comms.h"
#include <stddef.h>
#include <string.h>

extern const CommsDriver espnow_driver;
extern const CommsDriver ble_driver;

static const CommsDriver *drivers[] = {&espnow_driver, &ble_driver};

#define NUM_DRIVERS (sizeof(drivers) / sizeof(drivers[0]))

static const CommsDriver *active_driver = &espnow_driver;

static comms_recv_cb_t registered_recv_cb = NULL;
static comms_send_cb_t registered_send_cb = NULL;
static comms_discovery_cb_t registered_discovery_cb = NULL;

esp_err_t comms_init(void) {
  if (active_driver && active_driver->init) {
    esp_err_t err = active_driver->init();
    if (err == ESP_OK) {
      if (registered_recv_cb && active_driver->register_recv_cb) {
        active_driver->register_recv_cb(registered_recv_cb);
      }
      if (registered_send_cb && active_driver->register_send_cb) {
        active_driver->register_send_cb(registered_send_cb);
      }
      if (registered_discovery_cb && active_driver->register_discovery_cb) {
        active_driver->register_discovery_cb(registered_discovery_cb);
      }
    }
    return err;
  }
  return ESP_ERR_INVALID_STATE;
}

esp_err_t comms_deinit(void) {
  if (active_driver && active_driver->deinit) {
    return active_driver->deinit();
  }
  return ESP_ERR_INVALID_STATE;
}

bool comms_is_initialized(void) {
  if (active_driver && active_driver->is_initialized) {
    return active_driver->is_initialized();
  }
  return false;
}

esp_err_t comms_register_recv_cb(comms_recv_cb_t cb) {
  registered_recv_cb = cb;
  if (active_driver && active_driver->register_recv_cb) {
    return active_driver->register_recv_cb(cb);
  }
  return ESP_ERR_INVALID_STATE;
}

esp_err_t comms_register_send_cb(comms_send_cb_t cb) {
  registered_send_cb = cb;
  if (active_driver && active_driver->register_send_cb) {
    return active_driver->register_send_cb(cb);
  }
  return ESP_ERR_INVALID_STATE;
}

esp_err_t comms_register_discovery_cb(comms_discovery_cb_t cb) {
  registered_discovery_cb = cb;
  if (active_driver && active_driver->register_discovery_cb) {
    return active_driver->register_discovery_cb(cb);
  }
  return ESP_ERR_INVALID_STATE;
}

esp_err_t comms_send(const uint8_t *peer_mac, const uint8_t *data, size_t len) {
  if (active_driver && active_driver->send) {
    return active_driver->send(peer_mac, data, len);
  }
  return ESP_ERR_INVALID_STATE;
}

esp_err_t comms_connect_peer(const uint8_t *peer_mac, uint8_t channel) {
  if (active_driver && active_driver->connect_peer) {
    return active_driver->connect_peer(peer_mac, channel);
  }
  return ESP_ERR_INVALID_STATE;
}

esp_err_t comms_disconnect_peer(const uint8_t *peer_mac) {
  if (active_driver && active_driver->disconnect_peer) {
    return active_driver->disconnect_peer(peer_mac);
  }
  return ESP_ERR_INVALID_STATE;
}

bool comms_peer_exists(const uint8_t *peer_mac) {
  if (active_driver && active_driver->peer_exists) {
    return active_driver->peer_exists(peer_mac);
  }
  return false;
}

uint8_t comms_get_peer_channel(const uint8_t *peer_mac) {
  if (active_driver && active_driver->get_peer_channel) {
    return active_driver->get_peer_channel(peer_mac);
  }
  return 0;
}

esp_err_t comms_set_channel(uint8_t channel) {
  if (active_driver && active_driver->set_channel) {
    return active_driver->set_channel(channel);
  }
  return ESP_ERR_INVALID_STATE;
}

esp_err_t comms_select_driver(CommsType type) {
  const CommsDriver *target = NULL;
  for (size_t i = 0; i < NUM_DRIVERS; i++) {
    if (drivers[i]->type == type) {
      target = drivers[i];
      break;
    }
  }
  if (!target) {
    return ESP_ERR_NOT_FOUND;
  }

  if (active_driver == target) {
    return ESP_OK;
  }

  bool was_init = comms_is_initialized();
  if (was_init) {
    comms_deinit();
  }

  active_driver = target;

  if (was_init) {
    return comms_init();
  }
  return ESP_OK;
}

CommsType comms_get_active_type(void) {
  if (active_driver) {
    return active_driver->type;
  }
  return COMMS_TYPE_ESPNOW;
}

bool comms_is_same_mac(const uint8_t *mac1, const uint8_t *mac2) {
  return memcmp(mac1, mac2, COMMS_MAC_LEN) == 0;
}

uint8_t *comms_prepend_headers(const uint8_t *data, size_t len, CommsType type, size_t *out_len) {
  size_t header_len = (type == COMMS_TYPE_BLE) ? 2 : 1;
  *out_len = len + header_len;

  uint8_t *wrapped = (uint8_t *)malloc(*out_len);
  if (!wrapped) {
    return NULL;
  }

  if (type == COMMS_TYPE_BLE) {
    wrapped[0] = VESC_COMM_CUSTOM_APP_DATA;
    wrapped[1] = PUBMOTE_MAGIC;
    memcpy(wrapped + 2, data, len);
  }
  else {
    wrapped[0] = PUBMOTE_MAGIC;
    memcpy(wrapped + 1, data, len);
  }

  return wrapped;
}

bool comms_strip_headers(const uint8_t **data_ptr, int *len_ptr, CommsType type) {
  const uint8_t *data = *data_ptr;
  int len = *len_ptr;

  if (type == COMMS_TYPE_BLE) {
    if (len > 0 && data[0] == VESC_COMM_CUSTOM_APP_DATA) {
      data++;
      len--;
    }
    else {
      return false;
    }
  }

  if (len > 0 && data[0] == PUBMOTE_MAGIC) {
    data++;
    len--;
  }
  else {
    return false;
  }

  *data_ptr = data;
  *len_ptr = len;
  return true;
}
