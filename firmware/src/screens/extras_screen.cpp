#include "screens/extras_screen.h"
#include "charge/charge_driver.h"
#include "esp_system.h"
#include "remote/haptic.h"
#include "remote/powermanagement.h"
#include "remote/radio_session.h"
#include "ui/slint_window.h"
#include "utilities/ui_operation.h"

enum class ExtrasAction {
  None,
  FactoryReset,
  Shipping
};
static ExtrasAction pending_action = ExtrasAction::None;

static bool active() {
  return get_slint_window() && get_slint_window()->global<UiState>().get_screen() == Screen::Extras;
}

extern "C" void update_extras_properties() {
  if (!active())
    return;
  const auto &ui = get_slint_window()->global<UiState>();
  ui.set_shipping_mode_supported(charge_driver_supports_shipping_mode());
  ui.set_shipping_mode_available(!power_management_is_power_connected());
  if (power_management_take_shipping_error())
    ui.set_boot_notice("Shipping mode failed. Unplug USB and try again.");
}

extern "C" void setup_extras_properties() {
  pending_action = ExtrasAction::None;
  update_extras_properties();
  const auto &ui = get_slint_window()->global<UiState>();
  ui.on_confirm_dialog_accepted([]() {
    const auto action = pending_action;
    pending_action = ExtrasAction::None;
    get_slint_window()->global<UiState>().set_show_confirm_dialog(false);
    if (!active())
      return;
    if (action == ExtrasAction::FactoryReset) {
      ui_operation_start("Resetting settings...", radio_session_reset_settings, []() { esp_restart(); });
    }
    else if (action == ExtrasAction::Shipping) {
      ui_operation_start(
          "Preparing for storage...", []() { return ESP_OK; },
          []() {
            if (power_management_enter_shipping_mode() != ESP_OK)
              get_slint_window()->global<UiState>().set_boot_notice("Unplug USB before entering shipping mode.");
          });
    }
  });
  ui.on_confirm_dialog_rejected([]() {
    pending_action = ExtrasAction::None;
    get_slint_window()->global<UiState>().set_show_confirm_dialog(false);
  });
}

extern "C" void teardown_extras_properties() {
  pending_action = ExtrasAction::None;
  if (!get_slint_window())
    return;
  const auto &ui = get_slint_window()->global<UiState>();
  ui.on_confirm_dialog_accepted([]() {});
  ui.on_confirm_dialog_rejected([]() {});
  ui.set_show_confirm_dialog(false);
}

extern "C" void handle_open_extras() {
  if (!get_slint_window() || get_slint_window()->global<UiState>().get_screen() != Screen::About)
    return;
  haptic_vibrate(HAPTIC_DOUBLE_CLICK);
  get_slint_window()->global<UiState>().set_screen(Screen::Extras);
}

extern "C" void handle_extras_back() {
  if (active())
    get_slint_window()->global<UiState>().set_screen(Screen::About);
}

extern "C" void handle_extras_factory_reset() {
  if (!active())
    return;
  pending_action = ExtrasAction::FactoryReset;
  const auto &ui = get_slint_window()->global<UiState>();
  ui.set_confirm_dialog_title("Factory reset");
  ui.set_confirm_dialog_message("This will wipe all settings. Proceed?");
  ui.set_confirm_dialog_confirm_text("Reset");
  ui.set_confirm_dialog_variant("danger");
  ui.set_show_confirm_dialog(true);
}

extern "C" void handle_extras_shipping_mode() {
  if (!active() || !charge_driver_supports_shipping_mode())
    return;
  const auto &ui = get_slint_window()->global<UiState>();
  if (power_management_is_power_connected()) {
    ui.set_boot_notice("Unplug USB before entering shipping mode.");
    return;
  }
  pending_action = ExtrasAction::Shipping;
  ui.set_confirm_dialog_title("Shipping mode");
  ui.set_confirm_dialog_message(
      "Disconnect battery power for long-term storage? The remote will turn off. Connect USB to wake it again.");
  ui.set_confirm_dialog_confirm_text("Enter");
  ui.set_confirm_dialog_variant("standard");
  ui.set_show_confirm_dialog(true);
}
