#include "comms.h"
#include "utilities/vesc_utils.h"
#include <esp_log.h>
#include <string.h>

#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "PUBREMOTE-BLE-DRV";

static bool is_initialized = false;
static bool is_synced = false;
static comms_recv_cb_t registered_recv_cb = NULL;
static comms_send_cb_t registered_send_cb = NULL;
static comms_discovery_cb_t registered_discovery_cb = NULL;

static uint16_t ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t nus_tx_handle = 0; // Remote writes to this (VESC RX)
static uint16_t nus_rx_handle = 0; // Remote receives notifies from this (VESC TX)
static bool cccd_subscribed = false;
static bool discovery_complete = false;

// Reconnection tracking
static uint8_t target_peer_mac[6] = {0};
static uint8_t target_peer_addr_type = BLE_ADDR_RANDOM;
static bool has_target_peer = false;
static esp_timer_handle_t reconnect_timer = NULL;

// Cache scanned devices to get their address type when connecting
struct ScanCacheEntry {
  uint8_t mac[6];
  uint8_t addr_type;
};
static struct ScanCacheEntry scan_cache[16];
static int scan_cache_count = 0;

// VESC Packet reassembly buffer
static uint8_t rx_stream_buf[512];
static uint16_t rx_stream_len = 0;

// Nordic UART Service UUIDs
static ble_uuid128_t nus_svc_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

static ble_uuid128_t nus_tx_chr_uuid = BLE_UUID128_INIT( // Write
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

static ble_uuid128_t nus_rx_chr_uuid = BLE_UUID128_INIT( // Notify
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void start_scan(void);
static void stop_scan(void);
static esp_err_t ble_driver_send(const uint8_t *peer_mac, const uint8_t *data, size_t len);

// Forward declarations of GATT callbacks
static int ble_on_disc_chrs(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr,
                            void *arg);
static int ble_on_write_cccd(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr,
                             void *arg);

static void subscribe_to_notifications(uint16_t conn_handle, uint16_t val_handle) {
  uint16_t cccd_val = 0x0001; // Enable notifications
  ESP_LOGI(TAG, "Subscribing to notifications on handle %d", val_handle);
  int rc = ble_gattc_write_flat(conn_handle, val_handle + 1, &cccd_val, sizeof(cccd_val), ble_on_write_cccd, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to write CCCD descriptor; rc=%d", rc);
  }
}

static void send_connection_init_dummy(void) {
  // Perform a dummy write to VESC RX (nus_tx_handle) to initialize connection on stock VESC Express firmware.
  // Stock VESC Express firmware only sets the active BLE connection handle/interface ID upon receiving a write,
  // so sending this packet immediately on connect breaks the deadlock when pairing/communicating.
  if (nus_tx_handle != 0) {
    ESP_LOGI(TAG, "Sending connection-init dummy packet to VESC Express...");
    uint8_t dummy_packet[5] = {10, 0, 0, 0, 0}; // 10 is REM_PAIR_INIT, safely ignored by VESC Lisp
    ble_driver_send(target_peer_mac, dummy_packet, sizeof(dummy_packet));
  }
  else {
    ESP_LOGW(TAG, "Cannot send dummy packet: nus_tx_handle is still 0");
  }
}

static int ble_on_write_cccd(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr,
                             void *arg) {
  if (error->status == 0) {
    ESP_LOGI(TAG, "Successfully subscribed to notifications");
    cccd_subscribed = true;
    if (discovery_complete) {
      send_connection_init_dummy();
    }
  }
  else {
    ESP_LOGE(TAG, "CCCD write failed; status=%d", error->status);
  }
  return 0;
}

static int ble_on_disc_chrs(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr,
                            void *arg) {
  if (error->status == 0) {
    if (ble_uuid_cmp(&chr->uuid.u, &nus_tx_chr_uuid.u) == 0) {
      nus_tx_handle = chr->val_handle;
      ESP_LOGI(TAG, "Discovered NUS TX (Write) Characteristic: handle=%d", nus_tx_handle);
    }
    else if (ble_uuid_cmp(&chr->uuid.u, &nus_rx_chr_uuid.u) == 0) {
      nus_rx_handle = chr->val_handle;
      ESP_LOGI(TAG, "Discovered NUS RX (Notify) Characteristic: handle=%d", nus_rx_handle);
      subscribe_to_notifications(conn_handle, chr->val_handle);
    }
  }
  else if (error->status == BLE_HS_EDONE) {
    ESP_LOGI(TAG, "Characteristic discovery complete");
    discovery_complete = true;
    if (cccd_subscribed) {
      send_connection_init_dummy();
    }
  }
  else {
    ESP_LOGE(TAG, "Characteristic discovery failed; status=%d", error->status);
  }
  return 0;
}

static int ble_on_disc_svc(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_svc *service,
                           void *arg) {
  if (error->status == 0) {
    ESP_LOGI(TAG, "Discovered NUS Service: start_handle=%d, end_handle=%d", service->start_handle, service->end_handle);
    ble_gattc_disc_all_chrs(conn_handle, service->start_handle, service->end_handle, ble_on_disc_chrs, NULL);
  }
  else if (error->status == BLE_HS_EDONE) {
    ESP_LOGD(TAG, "Service discovery complete");
  }
  else {
    ESP_LOGE(TAG, "Service discovery failed; status=%d", error->status);
  }
  return 0;
}

static void start_service_discovery(uint16_t conn_handle) {
  ESP_LOGI(TAG, "Starting service discovery...");
  ble_gattc_disc_svc_by_uuid(conn_handle, &nus_svc_uuid.u, ble_on_disc_svc, NULL);
}

static void on_reconnect_timer(void *arg) {
  if (has_target_peer && ble_conn_handle == BLE_HS_CONN_HANDLE_NONE && is_initialized && is_synced) {
    ESP_LOGI(TAG, "Reconnection timer fired, retrying BLE connection...");

    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
      ESP_LOGE(TAG, "Error determining address type; rc=%d", rc);
      return;
    }

    ble_addr_t peer_addr;
    peer_addr.type = target_peer_addr_type;
    memcpy(peer_addr.val, target_peer_mac, 6);

    rc = ble_gap_connect(own_addr_type, &peer_addr, 30000, NULL, ble_gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
      ESP_LOGE(TAG, "Reconnect connect call failed; rc=%d", rc);
    }
  }
}

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
  case BLE_GAP_EVENT_DISC: {
    struct ble_hs_adv_fields fields;
    int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);

    char name_buf[32] = {0};
    if (rc == 0 && fields.name != NULL) {
      int len = fields.name_len < 31 ? fields.name_len : 31;
      memcpy(name_buf, fields.name, len);
      name_buf[len] = '\0';
    }

    ESP_LOGI(TAG, "Scan event: MAC=%02X:%02X:%02X:%02X:%02X:%02X, RSSI=%d, parse_rc=%d, Name='%s'",
             event->disc.addr.val[5], event->disc.addr.val[4], event->disc.addr.val[3], event->disc.addr.val[2],
             event->disc.addr.val[1], event->disc.addr.val[0], event->disc.rssi, rc, name_buf);

    if (rc != 0) {
      return 0;
    }

    bool supports_nus = false;
    if (fields.uuids128 != NULL) {
      for (int i = 0; i < fields.num_uuids128; i++) {
        const uint8_t nus_uuid128[16] = {0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                                         0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e};
        ESP_LOGI(TAG, "  Adv UUID128: %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                 fields.uuids128[i].value[15], fields.uuids128[i].value[14], fields.uuids128[i].value[13],
                 fields.uuids128[i].value[12], fields.uuids128[i].value[11], fields.uuids128[i].value[10],
                 fields.uuids128[i].value[9], fields.uuids128[i].value[8], fields.uuids128[i].value[7],
                 fields.uuids128[i].value[6], fields.uuids128[i].value[5], fields.uuids128[i].value[4],
                 fields.uuids128[i].value[3], fields.uuids128[i].value[2], fields.uuids128[i].value[1],
                 fields.uuids128[i].value[0]);
        if (memcmp(fields.uuids128[i].value, nus_uuid128, 16) == 0) {
          supports_nus = true;
          break;
        }
      }
    }

    bool matches = supports_nus;
    if (!matches && strlen(name_buf) > 0) {
      if (strstr(name_buf, "VESC") || strstr(name_buf, "Express") || strstr(name_buf, "PubRemote")) {
        matches = true;
      }
    }

    if (matches) {
      ESP_LOGI(TAG, "  -> MATCHED!");
      // Cache scanned device's address type
      bool exists = false;
      for (int i = 0; i < scan_cache_count; i++) {
        if (memcmp(scan_cache[i].mac, event->disc.addr.val, 6) == 0) {
          exists = true;
          break;
        }
      }
      if (!exists && scan_cache_count < 16) {
        memcpy(scan_cache[scan_cache_count].mac, event->disc.addr.val, 6);
        scan_cache[scan_cache_count].addr_type = event->disc.addr.type;
        scan_cache_count++;
      }

      if (registered_discovery_cb) {
        registered_discovery_cb(event->disc.addr.val, strlen(name_buf) > 0 ? name_buf : "VESC BLE", event->disc.rssi);
      }
    }
    return 0;
  }

  case BLE_GAP_EVENT_CONNECT: {
    if (event->connect.status == 0) {
      ESP_LOGI(TAG, "BLE Connected successfully! handle=%d", event->connect.conn_handle);
      ble_conn_handle = event->connect.conn_handle;
      cccd_subscribed = false;
      discovery_complete = false;
      start_service_discovery(ble_conn_handle);
    }
    else {
      ESP_LOGE(TAG, "BLE Connection failed; status=%d", event->connect.status);
      ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      if (has_target_peer && reconnect_timer) {
        esp_timer_start_once(reconnect_timer, 2000000);
      }
    }
    return 0;
  }

  case BLE_GAP_EVENT_DISC_COMPLETE:
    ESP_LOGI(TAG, "BLE Discovery complete");
    return 0;

  case BLE_GAP_EVENT_DISCONNECT: {
    ESP_LOGI(TAG, "BLE Disconnected; reason=%d", event->disconnect.reason);
    ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    nus_tx_handle = 0;
    nus_rx_handle = 0;
    if (has_target_peer && reconnect_timer) {
      esp_timer_start_once(reconnect_timer, 2000000);
    }
    return 0;
  }

  case BLE_GAP_EVENT_NOTIFY_RX: {
    if (event->notify_rx.attr_handle == nus_rx_handle) {
      uint16_t data_len = OS_MBUF_PKTLEN(event->notify_rx.om);
      uint8_t *data_buf = (uint8_t *)malloc(data_len);
      if (data_buf) {
        int rc = os_mbuf_copydata(event->notify_rx.om, 0, data_len, data_buf);
        if (rc == 0) {
          // Append incoming notification bytes to rx stream buffer
          if (rx_stream_len + data_len > sizeof(rx_stream_buf)) {
            ESP_LOGW(TAG, "RX stream buffer overflow, clearing buffer");
            rx_stream_len = 0;
          }
          if (rx_stream_len + data_len <= sizeof(rx_stream_buf)) {
            memcpy(&rx_stream_buf[rx_stream_len], data_buf, data_len);
            rx_stream_len += data_len;
          }

          // Try to parse VESC packets from stream buffer using VESC utility parser
          uint16_t parse_idx = 0;
          while (parse_idx < rx_stream_len) {
            uint8_t payload[512];
            size_t payload_len = 0;
            size_t bytes_consumed = 0;

            int res = vesc_parse_packet(&rx_stream_buf[parse_idx], rx_stream_len - parse_idx, payload, sizeof(payload),
                                        &payload_len, &bytes_consumed);
            if (res == 1) {
              // Packet successfully parsed, dispatch payload
              if (registered_recv_cb) {
                struct ble_gap_conn_desc desc;
                uint8_t peer_mac[6] = {0};
                uint8_t peer_addr_type = BLE_ADDR_RANDOM;
                if (ble_gap_conn_find(event->notify_rx.conn_handle, &desc) == 0) {
                  memcpy(peer_mac, desc.peer_id_addr.val, 6);
                  peer_addr_type = desc.peer_id_addr.type;
                }
                registered_recv_cb(peer_mac, payload, payload_len, peer_addr_type, -50);
              }
              parse_idx += bytes_consumed;
            }
            else if (res == 0) {
              // Incomplete packet, wait for more data
              break;
            }
            else {
              // Invalid packet or buffer too small, consume bad bytes and retry
              parse_idx += bytes_consumed;
            }
          }

          // Shift parsed bytes out of stream buffer
          if (parse_idx > 0) {
            if (parse_idx < rx_stream_len) {
              memmove(rx_stream_buf, &rx_stream_buf[parse_idx], rx_stream_len - parse_idx);
              rx_stream_len -= parse_idx;
            }
            else {
              rx_stream_len = 0;
            }
          }
        }
        free(data_buf);
      }
    }
    return 0;
  }

  case BLE_GAP_EVENT_MTU:
    ESP_LOGI(TAG, "MTU size updated to %d; conn_handle=%d", event->mtu.value, event->mtu.conn_handle);
    return 0;

  default:
    break;
  }
  return 0;
}

static void start_scan(void) {
  if (!is_initialized || !is_synced)
    return;

  uint8_t own_addr_type;
  int rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error determining address type; rc=%d", rc);
    return;
  }

  struct ble_gap_disc_params disc_params = {0};
  disc_params.filter_duplicates = 1;
  disc_params.passive = 0; // Active scanning to receive scan response packets (device names)
  disc_params.itvl = 0;
  disc_params.window = 0;
  disc_params.filter_policy = 0;
  disc_params.limited = 0;

  rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, ble_gap_event, NULL);
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    ESP_LOGE(TAG, "Error initiating BLE discovery; rc=%d", rc);
  }
  else {
    ESP_LOGI(TAG, "BLE discovery started successfully");
  }
}

static void stop_scan(void) {
  int rc = ble_gap_disc_cancel();
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    ESP_LOGE(TAG, "Failed to cancel BLE scan; rc=%d", rc);
  }
  else {
    ESP_LOGI(TAG, "BLE discovery stopped");
  }
}

static void ble_on_sync(void) {
  ESP_LOGI(TAG, "BLE Host synced");
  is_synced = true;

  int rc = ble_hs_util_ensure_addr(0);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to ensure address; rc=%d", rc);
  }

  // If a scan was requested before sync completed, start it now
  if (registered_discovery_cb) {
    start_scan();
  }

  // If a connection was requested before sync completed, connect now
  if (has_target_peer && ble_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    ESP_LOGI(TAG, "Synced: connecting to deferred target peer...");
    uint8_t own_addr_type;
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc == 0) {
      ble_addr_t peer_addr;
      peer_addr.type = target_peer_addr_type;
      memcpy(peer_addr.val, target_peer_mac, 6);
      ble_gap_connect(own_addr_type, &peer_addr, 30000, NULL, ble_gap_event, NULL);
    }
  }
}

static void ble_on_reset(int reason) {
  ESP_LOGE(TAG, "BLE host reset; reason=%d", reason);
  is_synced = false;
}

static void ble_host_task(void *param) {
  ESP_LOGI(TAG, "BLE Host Task Started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

static esp_err_t ble_driver_init(void) {
  if (is_initialized) {
    return ESP_OK;
  }

  // Yield to allow idle task to free memory deallocated during Wi-Fi deinit
  vTaskDelay(pdMS_TO_TICKS(200));

  ESP_LOGI(TAG, "Initializing BLE driver...");
  ESP_LOGI(TAG, "Free heap: %lu bytes (Internal DRAM: %lu bytes, Max Block: %lu bytes)",
           (unsigned long)esp_get_free_heap_size(), (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

  is_synced = false;
  has_target_peer = false;
  scan_cache_count = 0;
  ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
  nus_tx_handle = 0;
  nus_rx_handle = 0;
  rx_stream_len = 0;

  int rc = nimble_port_init();
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to initialize nimble port; rc=%d. Free heap: %lu (Internal: %lu, Max Block: %lu)", rc,
             (unsigned long)esp_get_free_heap_size(), (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return ESP_FAIL;
  }

  ble_hs_cfg.sync_cb = ble_on_sync;
  ble_hs_cfg.reset_cb = ble_on_reset;

  ble_svc_gap_init();
  ble_svc_gatt_init();

  rc = ble_svc_gap_device_name_set("PubRemote Client");
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to set device name; rc=%d", rc);
  }

  esp_timer_create_args_t timer_args = {.callback = on_reconnect_timer, .name = "ble_reconnect"};
  esp_timer_create(&timer_args, &reconnect_timer);

  nimble_port_freertos_init(ble_host_task);

  is_initialized = true;
  return ESP_OK;
}

static esp_err_t ble_driver_deinit(void) {
  if (!is_initialized) {
    return ESP_OK;
  }
  ESP_LOGI(TAG, "Deinitializing BLE driver...");

  has_target_peer = false;
  rx_stream_len = 0;
  if (reconnect_timer) {
    esp_timer_stop(reconnect_timer);
    esp_timer_delete(reconnect_timer);
    reconnect_timer = NULL;
  }

  if (ble_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(ble_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (ble_gap_conn_active()) {
    ESP_LOGI(TAG, "Canceling active connection attempt during deinit");
    ble_gap_conn_cancel();
  }

  ble_gap_disc_cancel();

  int rc = nimble_port_stop();
  if (rc != 0) {
    ESP_LOGE(TAG, "nimble_port_stop failed: %d", rc);
  }

  nimble_port_deinit();

  is_initialized = false;
  is_synced = false;
  return ESP_OK;
}

static bool ble_driver_is_initialized(void) {
  return is_initialized;
}

static esp_err_t ble_driver_register_recv_cb(comms_recv_cb_t cb) {
  registered_recv_cb = cb;
  return ESP_OK;
}

static esp_err_t ble_driver_register_send_cb(comms_send_cb_t cb) {
  registered_send_cb = cb;
  return ESP_OK;
}

static esp_err_t ble_driver_register_discovery_cb(comms_discovery_cb_t cb) {
  registered_discovery_cb = cb;
  if (cb != NULL) {
    start_scan();
  }
  else {
    stop_scan();
  }
  return ESP_OK;
}

static esp_err_t ble_driver_send(const uint8_t *peer_mac, const uint8_t *data, size_t len) {
  if (ble_conn_handle == BLE_HS_CONN_HANDLE_NONE || nus_tx_handle == 0) {
    ESP_LOGD(TAG, "ble_driver_send failed: ble_conn_handle=%d, nus_tx_handle=%d", ble_conn_handle, nus_tx_handle);
    if (registered_send_cb) {
      registered_send_cb(peer_mac, false);
    }
    return ESP_ERR_INVALID_STATE;
  }

  size_t wrapped_len = 0;
  uint8_t *wrapped_payload = comms_prepend_headers(data, len, COMMS_TYPE_BLE, &wrapped_len);
  if (!wrapped_payload) {
    if (registered_send_cb) {
      registered_send_cb(peer_mac, false);
    }
    return ESP_ERR_NO_MEM;
  }

  // Wrap payload in VESC packet frame using VESC utility wrapper
  uint8_t *tx_buf = (uint8_t *)malloc(wrapped_len + 6);
  if (!tx_buf) {
    free(wrapped_payload);
    if (registered_send_cb) {
      registered_send_cb(peer_mac, false);
    }
    return ESP_ERR_NO_MEM;
  }

  size_t tx_len = vesc_wrap_packet(wrapped_payload, wrapped_len, tx_buf, wrapped_len + 6);
  free(wrapped_payload);
  if (tx_len == 0) {
    free(tx_buf);
    if (registered_send_cb) {
      registered_send_cb(peer_mac, false);
    }
    return ESP_FAIL;
  }

  // Retrieve current ATT MTU of connection (subtract 3 bytes for ATT header)
  uint16_t mtu = ble_att_mtu(ble_conn_handle);
  size_t max_payload = (mtu > 3) ? (mtu - 3) : 20;

  size_t sent_bytes = 0;
  esp_err_t result_err = ESP_OK;

  while (sent_bytes < tx_len) {
    size_t chunk_len = tx_len - sent_bytes;
    if (chunk_len > max_payload) {
      chunk_len = max_payload;
    }

    int rc = ble_gattc_write_no_rsp_flat(ble_conn_handle, nus_tx_handle, tx_buf + sent_bytes, chunk_len);
    if (rc != 0) {
      ESP_LOGD(TAG, "ble_gattc_write_no_rsp_flat chunk failed; rc=%d", rc);
      if (rc == BLE_HS_ENOTCONN) {
        ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        nus_tx_handle = 0;
        nus_rx_handle = 0;
      }
      result_err = ESP_FAIL;
      break;
    }
    sent_bytes += chunk_len;
  }

  free(tx_buf);

  if (result_err != ESP_OK) {
    ESP_LOGD(TAG, "ble_gattc_write_no_rsp_flat failed; rc=%d", result_err);
    if (registered_send_cb) {
      registered_send_cb(peer_mac, false);
    }
    return result_err;
  }

  if (registered_send_cb) {
    registered_send_cb(peer_mac, true);
  }
  return ESP_OK;
}

static esp_err_t ble_driver_connect_peer(const uint8_t *peer_mac, uint8_t channel) {
  if (!is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "BLE connecting to peer: %02X:%02X:%02X:%02X:%02X:%02X", peer_mac[0], peer_mac[1], peer_mac[2],
           peer_mac[3], peer_mac[4], peer_mac[5]);

  stop_scan();

  memcpy(target_peer_mac, peer_mac, 6);
  has_target_peer = true;
  rx_stream_len = 0;

  uint8_t addr_type = BLE_ADDR_PUBLIC;
  bool found = false;
  for (int i = 0; i < scan_cache_count; i++) {
    if (memcmp(scan_cache[i].mac, peer_mac, 6) == 0) {
      addr_type = scan_cache[i].addr_type;
      found = true;
      break;
    }
  }

  if (!found) {
    uint8_t clean_channel = channel & 0x7F;
    if (clean_channel == 0 || clean_channel == 1) {
      addr_type = clean_channel;
    }
  }
  target_peer_addr_type = addr_type;

  if (ble_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(ble_conn_handle, &desc) == 0) {
      if (memcmp(desc.peer_id_addr.val, peer_mac, 6) == 0) {
        ESP_LOGI(TAG, "Already connected to peer");
        return ESP_OK;
      }
    }
  }

  if (!is_synced) {
    ESP_LOGI(TAG, "BLE Host not synced yet, connection deferred");
    return ESP_OK;
  }

  uint8_t own_addr_type;
  int rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error determining address type; rc=%d", rc);
    return ESP_FAIL;
  }

  ble_addr_t peer_addr;
  peer_addr.type = target_peer_addr_type;
  memcpy(peer_addr.val, target_peer_mac, 6);

  rc = ble_gap_connect(own_addr_type, &peer_addr, 30000, NULL, ble_gap_event, NULL);
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    ESP_LOGE(TAG, "Failed to connect; rc=%d", rc);
    return ESP_FAIL;
  }

  return ESP_OK;
}

static esp_err_t ble_driver_disconnect_peer(const uint8_t *peer_mac) {
  ESP_LOGI(TAG, "BLE disconnecting peer");
  has_target_peer = false;
  rx_stream_len = 0;
  if (reconnect_timer) {
    esp_timer_stop(reconnect_timer);
  }
  if (ble_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(ble_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
  }
  if (ble_gap_conn_active()) {
    ESP_LOGI(TAG, "Canceling active connection attempt");
    ble_gap_conn_cancel();
  }
  return ESP_OK;
}

static bool ble_driver_peer_exists(const uint8_t *peer_mac) {
  if (has_target_peer && memcmp(target_peer_mac, peer_mac, 6) == 0) {
    return true;
  }
  return false;
}

static uint8_t ble_driver_get_peer_channel(const uint8_t *peer_mac) {
  if (has_target_peer && memcmp(target_peer_mac, peer_mac, 6) == 0) {
    return target_peer_addr_type | 0x80;
  }
  return BLE_ADDR_RANDOM | 0x80;
}

static esp_err_t ble_driver_set_channel(uint8_t channel) {
  return ESP_OK;
}

const CommsDriver ble_driver = {.type = COMMS_TYPE_BLE,
                                .name = "BLE",
                                .init = ble_driver_init,
                                .deinit = ble_driver_deinit,
                                .is_initialized = ble_driver_is_initialized,
                                .register_recv_cb = ble_driver_register_recv_cb,
                                .register_send_cb = ble_driver_register_send_cb,
                                .register_discovery_cb = ble_driver_register_discovery_cb,
                                .send = ble_driver_send,
                                .connect_peer = ble_driver_connect_peer,
                                .disconnect_peer = ble_driver_disconnect_peer,
                                .peer_exists = ble_driver_peer_exists,
                                .get_peer_channel = ble_driver_get_peer_channel,
                                .set_channel = ble_driver_set_channel};

bool ble_is_connected(void) {
  return ble_conn_handle != BLE_HS_CONN_HANDLE_NONE && nus_tx_handle != 0;
}
