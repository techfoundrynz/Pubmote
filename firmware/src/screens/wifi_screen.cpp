#include "screens/wifi_screen.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "remote/radio_session.h"
#include "remote/settings.h"
#include "remote/wifi.h"
#include "slint_generated/app-window.h"
#include "ui/slint_window.h"
#include "utilities/keypad.h"
#include "utilities/ui_operation.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace
{
std::atomic<bool> active{false}, paused{false}, worker_running{false};
std::atomic<unsigned> generation{0};
std::atomic<unsigned> operation{0};
std::atomic<bool> leaving{false};
Screen destination = Screen::Menu, return_screen = Screen::Menu;
void restore_radio();
QueueHandle_t requests = nullptr;
struct Request {
  bool scan;
  unsigned revision;
  char ssid[33];
  char password[65];
};
Keypad editor;
bool editing_password = false;
std::string selected_ssid;
std::vector<wifi_network_info_t> found;

const auto &ui() {
  return get_slint_window()->global<UiState>();
}
bool current_session(unsigned token) {
  return active && token == generation;
}
bool current_operation(unsigned token, unsigned revision) {
  return current_session(token) && revision == operation;
}
void refresh_saved_network() {
  const char *ssid = get_wifi_ssid();
  ui().set_wifi_saved(ssid ? ssid : "");
}
void status(unsigned token, unsigned revision, const std::string &message, bool busy = false) {
  slint::invoke_from_event_loop([token, revision, message, busy]() {
    if (!current_operation(token, revision))
      return;
    ui().set_wifi_status(message.c_str());
    ui().set_wifi_busy(busy);
  });
}
void refresh_key_labels() {
  auto keys = std::make_shared<slint::VectorModel<slint::SharedString>>();
  for (int i = 0; i < 12; ++i)
    keys->push_back(editor.label(i).c_str());
  ui().set_wifi_keys(keys);
  ui().set_wifi_mode_label(editor.current_mode() == 0 ? "123" : editor.current_mode() == 1 ? "#+=" : "abc");
}
void refresh_editor() {
  ui().set_wifi_value(editor.value().c_str());
  ui().set_wifi_masked_value(std::string(editor.value().size(), '*').c_str());
}
void open_editor(bool password, const std::string &initial = "") {
  editing_password = password;
  editor.reset(initial, password ? 64 : 32);
  ui().set_wifi_secret(password);
  ui().set_wifi_revealed(false);
  ui().set_wifi_heading(password ? "PASSWORD" : "NETWORK NAME");
  refresh_key_labels();
  refresh_editor();
  ui().set_wifi_editing(true);
}
void close_editor() {
  editor.reset("", 32);
  ui().set_wifi_editing(false);
  ui().set_wifi_value("");
  ui().set_wifi_masked_value("");
  ui().set_wifi_revealed(false);
}
void input_error(const char *message) {
  ui().on_confirm_dialog_accepted([]() { ui().set_show_confirm_dialog(false); });
  ui().on_confirm_dialog_rejected([]() { ui().set_show_confirm_dialog(false); });
  ui().set_confirm_dialog_title("Check input");
  ui().set_confirm_dialog_message(message);
  ui().set_confirm_dialog_confirm_text("OK");
  ui().set_confirm_dialog_show_cancel(false);
  ui().set_show_confirm_dialog(true);
}
void worker(void *context) {
  const unsigned token = static_cast<unsigned>(reinterpret_cast<uintptr_t>(context));
  std::string joined_ssid;
  bool saved_ok = true;
  wifi_connection_state_t last_state = WIFI_STATE_DISCONNECTED;
  while (current_session(token)) {
    unsigned revision = operation.load();
    Request request{};
    if (xQueueReceive(requests, &request, pdMS_TO_TICKS(1000)) != pdTRUE) {
      auto connection = wifi_get_connection_state();
      if (connection == WIFI_STATE_CONNECTED) {
        char text[160];
        esp_netif_ip_info_t ip{};
        auto netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif)
          esp_netif_get_ip_info(netif, &ip);
        snprintf(text, sizeof(text), "%s\n" IPSTR " | %d dBm%s", joined_ssid.c_str(), IP2STR(&ip.ip), wifi_get_rssi(),
                 saved_ok ? "" : "\nCould not save network");
        status(token, revision, text);
      }
      else if (connection != last_state) {
        status(token, revision,
               connection == WIFI_STATE_RECONNECTING || connection == WIFI_STATE_CONNECTING
                   ? "Connection lost. Reconnecting..."
                   : "Disconnected. Scan or reconnect.",
               false);
      }
      last_state = connection;
      continue;
    }
    if (!current_session(token))
      break;
    revision = request.revision;
    if (request.scan) {
      status(token, revision, "Scanning...", true);
      wifi_network_info_t *networks = nullptr;
      uint16_t count = 0;
      esp_err_t result = wifi_scan_networks(&networks, &count);
      std::vector<wifi_network_info_t> entries;
      if (result == ESP_OK && count)
        entries.assign(networks, networks + count);
      wifi_free_network_list(networks);
      std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.rssi > b.rssi; });
      slint::invoke_from_event_loop([entries = std::move(entries), result, token, revision]() mutable {
        if (!current_operation(token, revision))
          return;
        found = std::move(entries);
        auto model = std::make_shared<slint::VectorModel<WifiNetwork>>();
        for (const auto &entry : found) {
          char detail[24];
          snprintf(detail, sizeof(detail), "%s %d", entry.password_protected ? "Lock" : "Open", entry.rssi);
          model->push_back(WifiNetwork{entry.ssid, detail});
        }
        ui().set_wifi_networks(model);
        ui().set_wifi_busy(false);
        ui().set_wifi_status(result != ESP_OK ? "Scan failed. Try again."
                             : found.empty()  ? "No networks found."
                                              : "Choose a network");
      });
    }
    else {
      wifi_set_auto_reconnect(false);
      wifi_disconnect();
      status(token, revision, "Connecting...", true);
      esp_err_t result = wifi_connect_to_network_cancellable(
          request.ssid, request.password,
          [](void *session) { return !current_session(static_cast<unsigned>(reinterpret_cast<uintptr_t>(session))); },
          context);
      if (result == ESP_OK && current_session(token)) {
        // Invalidate the old SSID first so a partial write cannot attach a new
        // password to a different saved network.
        esp_err_t saved = save_wifi_ssid("");
        if (saved == ESP_OK)
          saved = save_wifi_password(request.password);
        if (saved == ESP_OK)
          saved = save_wifi_ssid(request.ssid);
        joined_ssid = request.ssid;
        saved_ok = saved == ESP_OK;
        slint::invoke_from_event_loop([token, revision]() {
          if (current_operation(token, revision))
            refresh_saved_network();
        });
        status(token, revision, saved == ESP_OK ? "Connected. Network saved." : "Connected, but saving failed.");
        wifi_set_auto_reconnect(true);
      }
      else
        status(token, revision, "Connection failed. Check password or try another network.");
    }
    memset(&request, 0, sizeof(request));
  }
  worker_running = false;
  restore_radio();
  vTaskDelete(nullptr);
}
void restored(esp_err_t result, void *) {
  slint::invoke_from_event_loop([result]() {
    ui_processing_end([result]() {
      leaving = false;
      if (result == ESP_OK) {
        paused = false;
        ui().set_screen(destination);
      }
      else {
        ui().set_wifi_status("Could not restore board radio. Back retries.");
        ui().set_wifi_busy(true);
      }
    });
  });
}
void restore_radio() {
  esp_err_t result = radio_session_end(restored, nullptr);
  if (result != ESP_OK)
    restored(result, nullptr);
}
void prepared(esp_err_t result, void *context) {
  slint::invoke_from_event_loop([]() { ui_processing_end(); });
  const unsigned token = static_cast<unsigned>(reinterpret_cast<uintptr_t>(context));
  if (!current_session(token)) {
    worker_running = false;
    restore_radio();
    return;
  }
  if (result == ESP_OK && xTaskCreate(worker, "wifi-page", 6144, context, 4, nullptr) == pdPASS)
    return;
  worker_running = false;
  status(token, operation.load(), "Could not start Wi-Fi. Back restores the board radio.", true);
}
void submit(const Request &request) {
  if (ui().get_wifi_busy() || !active)
    return;
  if (!requests)
    requests = xQueueCreate(1, sizeof(Request));
  if (!requests) {
    ui().set_wifi_status("Not enough memory to start Wi-Fi.");
    return;
  }
  Request queued = request;
  queued.revision = ++operation;
  if (xQueueSend(requests, &queued, 0) == pdTRUE) {
    if (!worker_running) {
      worker_running = true;
      paused = true;
      auto context = reinterpret_cast<void *>(static_cast<uintptr_t>(generation.load()));
      ui_processing_begin("Starting Wi-Fi...");
      esp_err_t result = radio_session_begin(prepared, context);
      if (result != ESP_OK) {
        ui_processing_end();
        worker_running = false;
        paused = false;
        xQueueReset(requests);
        ui().set_wifi_status("Could not start Wi-Fi. Try again.");
        return;
      }
    }
    ui().set_wifi_busy(true);
    ui().set_wifi_status(request.scan ? "Starting scan..." : "Connecting...");
  }
  else {
    ui().set_wifi_status("Wi-Fi is busy. Try again.");
  }
  memset(&queued, 0, sizeof(queued));
}
void connect(const std::string &ssid, const std::string &password) {
  Request request{};
  if (ssid.empty() || ssid.size() > 32 || password.size() > 64)
    return;
  memcpy(request.ssid, ssid.data(), ssid.size());
  memcpy(request.password, password.data(), password.size());
  submit(request);
  memset(&request, 0, sizeof(request));
}
} // namespace

extern "C" void teardown_wifi_properties() {
  active = false;
  ++generation;
  close_editor();
  selected_ssid.clear();
  found.clear();
}
extern "C" void setup_wifi_properties() {
  active = true;
  leaving = false;
  ++generation;
  if (requests)
    xQueueReset(requests);
  ui().set_wifi_busy(false);
  ui().set_wifi_status("Scanning or connecting pauses your board radio. Back reconnects it.");
  refresh_saved_network();
  ui().set_wifi_networks(std::make_shared<slint::VectorModel<WifiNetwork>>());
  close_editor();
  ui().on_wifi_scan([]() {
    Request request{};
    request.scan = true;
    submit(request);
  });
  ui().on_wifi_manual([]() {
    selected_ssid.clear();
    open_editor(false);
  });
  ui().on_wifi_select([](int index) {
    if (index < 0 || static_cast<size_t>(index) >= found.size())
      return;
    selected_ssid = found[index].ssid;
    if (found[index].password_protected) {
      const char *saved = get_wifi_ssid();
      const char *password = saved && selected_ssid == saved ? get_wifi_password() : nullptr;
      open_editor(true, password ? password : "");
    }
    else
      connect(selected_ssid, "");
  });
  ui().on_wifi_connect_saved([]() {
    const char *ssid = get_wifi_ssid();
    const char *password = get_wifi_password();
    if (ssid)
      connect(ssid, password ? password : "");
  });
  ui().on_wifi_forget([]() {
    ui().on_confirm_dialog_accepted([]() {
      ui().set_show_confirm_dialog(false);
      ui_operation_start(
          "Forgetting network...",
          []() {
            esp_err_t result = save_wifi_ssid("");
            esp_err_t password_result = save_wifi_password("");
            return result == ESP_OK ? password_result : result;
          },
          []() {
            refresh_saved_network();
            ui().set_wifi_status("Saved network forgotten.");
          });
    });
    ui().on_confirm_dialog_rejected([]() { ui().set_show_confirm_dialog(false); });
    ui().set_confirm_dialog_title("Forget network?");
    ui().set_confirm_dialog_message("Remove the saved Wi-Fi credentials?");
    ui().set_confirm_dialog_confirm_text("Forget");
    ui().set_show_confirm_dialog(true);
  });
  ui().on_wifi_key([](int key) {
    bool was_uppercase = editor.is_uppercase();
    editor.press(key, esp_timer_get_time() / 1000);
    if (was_uppercase != editor.is_uppercase())
      refresh_key_labels();
    refresh_editor();
  });
  ui().on_wifi_mode([]() {
    editor.next_mode();
    refresh_key_labels();
    refresh_editor();
  });
  ui().on_wifi_cancel([]() { close_editor(); });
  ui().on_wifi_done([]() {
    editor.commit();
    if (!editing_password) {
      if (editor.value().empty()) {
        input_error("Enter a network name.");
        return;
      }
      selected_ssid = editor.value();
      open_editor(true);
    }
    else {
      const std::string password = editor.value();
      if (!password.empty() &&
          (password.size() < 8 ||
           (password.size() == 64 && password.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos))) {
        input_error("Use 8–63 password bytes or 64 hex digits. Leave empty for an open network.");
        return;
      }
      close_editor();
      connect(selected_ssid, password);
    }
  });
  ui().on_wifi_back([]() { ui().set_screen(return_screen); });
}
extern "C" void wifi_return_to_update(void) {
  return_screen = Screen::Update;
}
extern "C" bool wifi_screen_prepare_exit(int target) {
  if (!paused) {
    return_screen = Screen::Menu;
    return false;
  }
  destination = static_cast<Screen>(target);
  if (!leaving.exchange(true)) {
    active = false;
    ++generation;
    close_editor();
    ui().set_wifi_busy(true);
    ui().set_wifi_status("Restoring board connection...");
    ui_processing_begin("Restoring board connection...");
    if (!worker_running)
      restore_radio();
  }
  return true;
}
