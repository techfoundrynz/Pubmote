#include "screens/pairing_screen.h"
#include "esp_log.h"
#include "remote/comms.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/led.h"
#include "remote/settings.h"
#include "slint_generated/app-window.h"
#include "utilities/ui_operation.h"
#include <array>
#include <cstring>
#include <string>
#include <vector>

static const char *TAG = "PUBREMOTE-PAIRING_SCREEN";

struct DiscoveredBleDevice {
  uint8_t mac[6];
  std::string name;
};

static std::vector<DiscoveredBleDevice> discovered_ble_devices;
static bool exit_restored = false;

static void on_device_discovered(const uint8_t *mac, const char *name, int rssi) {
  std::array<uint8_t, 6> address;
  memcpy(address.data(), mac, address.size());
  slint::invoke_from_event_loop([address, name = std::string(name ? name : "Unknown VESC")]() {
    if (!is_pairing_screen_active())
      return;
    for (const auto &device : discovered_ble_devices)
      if (memcmp(device.mac, address.data(), address.size()) == 0)
        return;
    DiscoveredBleDevice device;
    memcpy(device.mac, address.data(), address.size());
    device.name = name;
    discovered_ble_devices.push_back(std::move(device));
    auto model = std::make_shared<slint::VectorModel<DiscoveredDevice>>();
    for (size_t i = 0; i < discovered_ble_devices.size(); ++i)
      model->push_back(DiscoveredDevice{discovered_ble_devices[i].name.c_str(), static_cast<int>(i)});
    get_slint_window()->global<UiState>().set_discovered_devices(model);
  });
}

// (Re)initialize the radio if needed and start BLE discovery, reflecting the
// outcome in the scan view: an init failure shows an error + Retry button
// instead of a silently-empty device list. The driver switch for a pairing
// attempt can fail under memory pressure (the BLE controller needs large
// contiguous internal RAM right after a WiFi deinit).
static void start_ble_scan() {
  discovered_ble_devices.clear();
  const auto &state = get_slint_window()->global<UiState>();
  state.set_discovered_devices(std::make_shared<slint::VectorModel<DiscoveredDevice>>());
  state.set_ble_scan_error("Scan stopped. Tap Retry.");
  ui_operation_start(
      "Starting Bluetooth scan...",
      []() {
        comms_disconnect_peer(pairing_settings.remote_addr);
        esp_err_t result = comms_init();
        if (result == ESP_OK)
          result = comms_register_discovery_cb(on_device_discovered);
        return result;
      },
      []() { get_slint_window()->global<UiState>().set_ble_scan_error(""); });
}

extern "C" void setup_pairing_properties() {
  ESP_LOGI(TAG, "Setting up pairing screen properties");
  led_set_effect_rainbow();
  exit_restored = false;
  connection_update_state(CONNECTION_STATE_DISCONNECTED);
  pairing_state = PAIRING_STATE_UNPAIRED;
  const bool is_ble = comms_get_active_type() == COMMS_TYPE_BLE;
  {
    const auto &state = get_slint_window()->global<UiState>();
    state.set_pairing_code("----");
    state.set_pairing_action_text("Cancel");
    state.set_pairing_status("Searching for board...");
    state.set_ble_scan_error("");
    state.set_is_ble_scan(is_ble);

    if (is_ble) {
      // Bind retry (shown when the radio failed to start)
      state.on_retry_ble_scan([]() { start_ble_scan(); });

      // Bind device selection
      state.on_select_device([](int idx) {
        if (idx < 0 || idx >= (int)discovered_ble_devices.size()) {
          return;
        }

        // Stop scan
        comms_register_discovery_cb(NULL);

        const auto &dev = discovered_ble_devices[idx];
        ESP_LOGI(TAG, "Selected BLE device: %s, connecting...", dev.name.c_str());

        // Connect to the device to trigger pairing handshake
        comms_connect_peer(dev.mac, 1);

        // Turn off BLE scan view to show pairing PIN display
        slint::invoke_from_event_loop([]() {
          if (get_slint_window()) {
            const auto &state = get_slint_window()->global<UiState>();
            state.set_is_ble_scan(false);
            state.set_pairing_code("----"); // Show dashed while waiting for PIN handshake
            state.set_pairing_status("Connecting to board...");
          }
        });
      });
    }
  }

  if (is_ble)
    start_ble_scan();
  else
    ui_operation_start("Starting board search...", []() {
      esp_err_t result = comms_disconnect_peer(pairing_settings.remote_addr);
      if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
        return result;
      return comms_init();
    });
}

extern "C" void handle_pairing_action() {
  ESP_LOGI(TAG, "Pairing cancel action");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Boards); });
}

extern "C" void teardown_pairing_properties() {
  ESP_LOGI(TAG, "Tearing down pairing screen properties");
  led_apply_mode();

  if (comms_get_active_type() == COMMS_TYPE_BLE) {
    comms_register_discovery_cb(NULL);
  }

  discovered_ble_devices.clear();
}
extern "C" bool pairing_screen_prepare_exit(int target) {
  if (exit_restored)
    return false;
  ui_operation_start(
      "Restoring board connection...",
      []() {
        comms_register_discovery_cb(nullptr);
        comms_disconnect_peer(pairing_settings.remote_addr);
        if (get_default_device_index() >= 0) {
          esp_err_t result = connection_switch_comms_mode(settings_get_active_comms_mode());
          if (result != ESP_OK)
            return result;
        }
        connection_refresh_pairing_state();
        if (pairing_state == PAIRING_STATE_PAIRED && connection_get_auto_reconnect())
          connection_connect_to_default_peer();
        return ESP_OK;
      },
      [target]() {
        exit_restored = true;
        get_slint_window()->global<UiState>().set_screen(static_cast<Screen>(target));
      });
  return true;
}
