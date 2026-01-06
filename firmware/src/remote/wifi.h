#ifndef __WIFI_H
#define __WIFI_H

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include <stdbool.h>
#include <stdint.h>

// Event group bits for WiFi events
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

/**
 * @brief WiFi connection states
 */
typedef enum {
  WIFI_STATE_DISCONNECTED = 0, ///< WiFi is disconnected
  WIFI_STATE_CONNECTING,       ///< WiFi is attempting to connect
  WIFI_STATE_CONNECTED,        ///< WiFi is connected and has IP
  WIFI_STATE_RECONNECTING      ///< WiFi is attempting to reconnect after disconnection
} wifi_connection_state_t;

/**
 * @brief Structure to hold WiFi network information
 */
typedef struct {
  char ssid[33];           ///< WiFi SSID (max 32 chars + null terminator)
  int rssi;                ///< Signal strength in dBm
  bool password_protected; ///< Whether network requires password
} wifi_network_info_t;

/**
 * @brief Initialize WiFi station mode after ESP-NOW
 *
 * This function transitions from ESP-NOW to WiFi station mode while keeping
 * the WiFi subsystem running. This is the primary initialization function
 * for applications that always start with ESP-NOW.
 *
 * @note Before calling this function, only call:
 *       - esp_now_deinit()
 *       - Do NOT call esp_wifi_stop() or esp_wifi_deinit()
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_NO_MEM: Memory allocation failed
 *     - Other ESP_ERR_* codes from underlying functions
 */
esp_err_t wifi_init(void);

/**
 * @brief Scan for available WiFi networks and return list
 *
 * @param[out] networks Pointer to array of network info structures (caller must free)
 * @param[out] network_count Number of networks found
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid parameters
 *     - ESP_ERR_NO_MEM: Memory allocation failed
 *     - Other ESP_ERR_* codes from WiFi scan functions
 */
esp_err_t wifi_scan_networks(wifi_network_info_t **networks, uint16_t *network_count);

/**
 * @brief Free the network list returned by wifi_scan_networks
 *
 * @param networks Pointer to network array to free
 */
void wifi_free_network_list(wifi_network_info_t *networks);

/**
 * @brief Connect to WiFi network with SSID and password
 *
 * This function stores credentials for automatic reconnection and attempts
 * to connect to the specified network.
 *
 * @param ssid WiFi network SSID (must not be NULL)
 * @param password WiFi password (can be NULL for open networks)
 *
 * @return
 *     - ESP_OK: Successfully connected
 *     - ESP_ERR_INVALID_ARG: Invalid SSID
 *     - ESP_FAIL: Connection failed after retries
 *     - ESP_ERR_TIMEOUT: Unexpected timeout
 *     - Other ESP_ERR_* codes from WiFi functions
 */
esp_err_t wifi_connect_to_network(const char *ssid, const char *password);

/**
 * @brief Disconnect from WiFi network
 *
 * This function disconnects from the current WiFi network and clears
 * stored credentials to prevent automatic reconnection.
 *
 * @return
 *     - ESP_OK: Successfully disconnected
 *     - Other ESP_ERR_* codes from esp_wifi_disconnect
 */
esp_err_t wifi_disconnect(void);

/**
 * @brief Uninitialize WiFi and clean up all resources
 *
 * This function stops WiFi, deinitializes the WiFi subsystem, and cleans up
 * all allocated resources including timers and event groups.
 *
 * @return
 *     - ESP_OK: Success
 *     - Other ESP_ERR_* codes from WiFi functions
 */
esp_err_t wifi_uninit(void);

/**
 * @brief Get current WiFi connection state
 *
 * @return Current WiFi connection state
 */
wifi_connection_state_t wifi_get_connection_state(void);

/**
 * @brief Get connection state as human-readable string
 *
 * @return String representation of current state
 */
const char *wifi_get_connection_state_string(void);

/**
 * @brief Enable or disable automatic reconnection
 *
 * When enabled, the WiFi module will automatically attempt to reconnect
 * if the connection is lost unexpectedly.
 *
 * @param enable true to enable auto-reconnect, false to disable
 */
void wifi_set_auto_reconnect(bool enable);

/**
 * @brief Check if automatic reconnection is enabled
 *
 * @return true if auto-reconnect is enabled, false otherwise
 */
bool wifi_is_auto_reconnect_enabled(void);

bool wifi_is_initialized();

/**
 * @brief Get current WiFi RSSI
 * 
 * @return RSSI in dBm, or 0 if not connected
 */
int8_t wifi_get_rssi(void);

#endif // __WIFI_H