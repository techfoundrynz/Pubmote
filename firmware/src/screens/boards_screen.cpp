#include "screens/boards_screen.h"
#include "esp_log.h"
#include "remote/settings.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "generated/app-window.h"

static const char *TAG = "PUBREMOTE-BOARDS_SCREEN";

extern "C" void setup_boards_properties() {
  ESP_LOGI(TAG, "Setting up boards screen properties");
  
  slint::invoke_from_event_loop([]() {
    const auto &state = get_slint_window()->global<UiState>();
    
    // Create a vector model of PairedBoard
    auto model = std::make_shared<slint::VectorModel<PairedBoard>>();
    
    int count = get_paired_device_count();
    int default_idx = get_default_device_index();
    
    for (int i = 0; i < count; i++) {
      PairedDevice device;
      if (get_paired_device(i, &device)) {
        char mac_str[24];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 device.mac[0], device.mac[1], device.mac[2],
                 device.mac[3], device.mac[4], device.mac[5]);
                 
        PairedBoard board;
        board.mac = slint::SharedString(mac_str);
        board.index = i;
        board.is_active = (i == default_idx);
        
        model->push_back(board);
      }
    }
    
    state.set_paired_boards(model);
  });
}

extern "C" void handle_select_board(int idx) {
  ESP_LOGI(TAG, "Selected board at index %d", idx);
  if (set_active_paired_device(idx)) {
    // Reconnect to the selected peer
    connection_connect_to_default_peer();
    
    // Go back to the Menu Screen
    slint::invoke_from_event_loop([]() {
      get_slint_window()->global<UiState>().set_screen(Screen::Menu);
    });
  }
}

extern "C" void handle_delete_board(int idx) {
  ESP_LOGI(TAG, "Delete board at index %d", idx);
  if (delete_paired_device_index(idx)) {
    // If the active peer was deleted, default_index changes, so reconnect
    connection_connect_to_default_peer();
    
    // Update the boards screen properties to reflect deletion
    setup_boards_properties();
  }
}

extern "C" void handle_boards_back() {
  ESP_LOGI(TAG, "Boards screen back action");
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::Menu);
  });
}

extern "C" void handle_boards_pair_new() {
  ESP_LOGI(TAG, "Boards screen pair new action");
  slint::invoke_from_event_loop([]() {
    get_slint_window()->global<UiState>().set_screen(Screen::Pairing);
  });
}
