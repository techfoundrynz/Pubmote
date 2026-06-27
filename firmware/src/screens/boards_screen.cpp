#include "screens/boards_screen.h"
#include "esp_log.h"
#include "generated/app-window.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/settings.h"

static const char *TAG = "PUBREMOTE-BOARDS_SCREEN";

#include <string>

static int pending_delete_idx = -1;

extern "C" void setup_boards_properties() {
  ESP_LOGI(TAG, "Setting up boards screen properties");

  slint::invoke_from_event_loop([]() {
    const auto &state = get_slint_window()->global<UiState>();

    pending_delete_idx = -1;

    // Register confirm dialog callbacks for this screen
    state.on_confirm_dialog_accepted([]() {
      slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_show_confirm_dialog(false); });

      if (pending_delete_idx != -1) {
        ESP_LOGI(TAG, "Confirming deletion of board at index %d", pending_delete_idx);
        if (delete_paired_device_index(pending_delete_idx)) {
          connection_connect_to_default_peer();
          setup_boards_properties();
        }
        pending_delete_idx = -1;
      }
    });

    state.on_confirm_dialog_rejected([]() {
      slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_show_confirm_dialog(false); });
      pending_delete_idx = -1;
    });

    // Create a vector model of PairedBoard
    auto model = std::make_shared<slint::VectorModel<PairedBoard>>();

    int count = get_paired_device_count();
    int default_idx = get_default_device_index();

    for (int i = 0; i < count; i++) {
      PairedDevice device;
      if (get_paired_device(i, &device)) {
        char mac_str[24];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", device.mac[0], device.mac[1], device.mac[2],
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
    slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Menu); });
  }
}

extern "C" void handle_delete_board(int idx) {
  ESP_LOGI(TAG, "Request to delete board at index %d", idx);

  pending_delete_idx = idx;

  char mac_str[24] = "this board";
  PairedDevice device;
  if (get_paired_device(idx, &device)) {
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", device.mac[0], device.mac[1], device.mac[2],
             device.mac[3], device.mac[4], device.mac[5]);
  }

  char message_str[128];
  snprintf(message_str, sizeof(message_str), "Are you sure you want to delete board %s?", mac_str);

  slint::invoke_from_event_loop([message = std::string(message_str)]() {
    const auto &state = get_slint_window()->global<UiState>();
    state.set_confirm_dialog_title("Delete Board");
    state.set_confirm_dialog_message(message.c_str());
    state.set_confirm_dialog_confirm_text("Delete");
    state.set_confirm_dialog_cancel_text("Cancel");
    state.set_confirm_dialog_variant("danger");
    state.set_show_confirm_dialog(true);
  });
}

extern "C" void handle_boards_back() {
  ESP_LOGI(TAG, "Boards screen back action");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Menu); });
}

extern "C" void handle_boards_pair_new() {
  ESP_LOGI(TAG, "Boards screen pair new action");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Pairing); });
}

extern "C" void teardown_boards_properties() {
  ESP_LOGI(TAG, "Tearing down boards screen properties");
  if (!get_slint_window())
    return;
  slint::invoke_from_event_loop([]() {
    const auto &state = get_slint_window()->global<UiState>();
    state.on_confirm_dialog_accepted([]() {});
    state.on_confirm_dialog_rejected([]() {});
  });
}
