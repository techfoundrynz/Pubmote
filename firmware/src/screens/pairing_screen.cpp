#include "screens/pairing_screen.h"
#include "esp_log.h"
#include "generated/app-window.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/led.h"
#include "remote/comms.h"
#include "remote/settings.h"
#include <vector>
#include <string>
#include <cstring>

static const char *TAG = "PUBREMOTE-PAIRING_SCREEN";

struct DiscoveredBleDevice {
  uint8_t mac[6];
  std::string name;
};

static std::vector<DiscoveredBleDevice> discovered_ble_devices;

static void on_device_discovered(const uint8_t *mac, const char *name, int rssi) {
  // Check if we already have it
  for (const auto &dev : discovered_ble_devices) {
    if (memcmp(dev.mac, mac, 6) == 0) {
      return; // Already added
    }
  }

  DiscoveredBleDevice new_dev;
  memcpy(new_dev.mac, mac, 6);
  new_dev.name = name ? name : "Unknown VESC";
  discovered_ble_devices.push_back(new_dev);

  ESP_LOGI(TAG, "Discovered BLE device: %s (%02X:%02X:%02X:%02X:%02X:%02X)", new_dev.name.c_str(),
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // Update Slint UI
  slint::invoke_from_event_loop([]() {
    if (!get_slint_window()) return;
    const auto &state = get_slint_window()->global<UiState>();
    
    auto model = std::make_shared<slint::VectorModel<DiscoveredDevice>>();
    for (size_t i = 0; i < discovered_ble_devices.size(); ++i) {
      DiscoveredDevice d;
      d.name = slint::SharedString(discovered_ble_devices[i].name);
      d.index = (int)i;
      model->push_back(d);
    }
    state.set_discovered_devices(model);
  });
}

extern "C" void setup_pairing_properties() {
  ESP_LOGI(TAG, "Setting up pairing screen properties");
  led_set_effect_rainbow();
  connection_update_state(CONNECTION_STATE_DISCONNECTED);
  pairing_state = PAIRING_STATE_UNPAIRED;

  slint::invoke_from_event_loop([]() {
    const auto &state = get_slint_window()->global<UiState>();
    state.set_pairing_code("0000");
    state.set_pairing_action_text("Cancel");

    if (comms_get_active_type() == COMMS_TYPE_BLE) {
      state.set_is_ble_scan(true);

      // Clear discovered list
      discovered_ble_devices.clear();
      auto model = std::make_shared<slint::VectorModel<DiscoveredDevice>>();
      state.set_discovered_devices(model);

      // Register discovery callback & start scan
      comms_register_discovery_cb(on_device_discovered);

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
          }
        });
      });
    } else {
      state.set_is_ble_scan(false);
    }
  });
}

extern "C" void handle_pairing_action() {
  ESP_LOGI(TAG, "Pairing cancel action");
  pairing_state = PAIRING_STATE_UNPAIRED;

  if (comms_get_active_type() == COMMS_TYPE_BLE) {
    comms_register_discovery_cb(NULL);
    comms_disconnect_peer(pairing_settings.remote_addr);
  }

  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Boards); });
}

extern "C" void teardown_pairing_properties() {
  ESP_LOGI(TAG, "Tearing down pairing screen properties");
  led_set_effect_default();

  if (comms_get_active_type() == COMMS_TYPE_BLE) {
    comms_register_discovery_cb(NULL);
  }
}
