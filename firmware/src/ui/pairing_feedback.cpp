#include "ui/pairing_feedback.h"
#include "ui/slint_window.h"
#include <cstdio>
#include <string>

extern "C" void pairing_ui_secret_received(uint32_t secret) {
  char text[32];
  snprintf(text, sizeof(text), "%ld", (long)secret);
  slint::SharedString pairing_code(text);
  slint::invoke_from_event_loop([pairing_code]() {
    if (auto *window = get_slint_window()) {
      window->global<UiState>().set_pairing_code(pairing_code);
      window->global<UiState>().set_pairing_status("Code received! Waiting for confirmation...");
    }
  });
}
extern "C" void pairing_ui_completed(void) {
  slint::invoke_from_event_loop([]() {
    if (auto *window = get_slint_window())
      window->global<UiState>().set_screen(Screen::Stats);
  });
}
extern "C" void pairing_ui_incompatible_receiver(uint8_t api_version, uint8_t minimum) {
  // Format error message for confirmation dialog
  char message_str[256];
  snprintf(message_str, sizeof(message_str),
           "The pubmote API version (%d) on the receiver is below the minimum required version (%d).\n\nPlease update "
           "your receiver.",
           api_version, minimum);

  slint::invoke_from_event_loop([message = std::string(message_str)]() {
    if (!get_slint_window())
      return;
    const auto &state = get_slint_window()->global<UiState>();

    // Register simple callbacks to close confirm dialog
    state.on_confirm_dialog_accepted([]() {
      slint::invoke_from_event_loop([]() {
        if (get_slint_window()) {
          get_slint_window()->global<UiState>().set_show_confirm_dialog(false);
        }
      });
    });

    state.on_confirm_dialog_rejected([]() {
      slint::invoke_from_event_loop([]() {
        if (get_slint_window()) {
          get_slint_window()->global<UiState>().set_show_confirm_dialog(false);
        }
      });
    });

    state.set_confirm_dialog_title("Update Required");
    state.set_confirm_dialog_message(message.c_str());
    state.set_confirm_dialog_confirm_text("OK");
    state.set_confirm_dialog_variant("danger");
    state.set_show_confirm_dialog(true);
  });
}
