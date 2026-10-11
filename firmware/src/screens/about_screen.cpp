#include "screens/about_screen.h"
#include "build_metadata.h"
#include "charge/charge_driver.h"
#include "config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "remote/connection.h"
#include "remote/stats.h"
#include "slint_generated/app-window.h"
#include "ui/slint_window.h"
#include <memory>
#include <stdio.h>
#include <string_view>

static const char *TAG = "PUBREMOTE-ABOUT_SCREEN";
// Accessed only from the Slint event loop. The UI retains the outgoing model
// during its exit animation after our reference is released.
static std::shared_ptr<slint::VectorModel<StatEntry>> stats_model;
static bool stats_has_current = false;

static const char *const charger_labels[] = {"USB power", "Source",      "Charge enabled",
                                             "Mode",      "Input limit", "Input regulation",
                                             "Fault now", "Last fault",  "Sample age"};
static constexpr size_t charger_row_count = sizeof(charger_labels) / sizeof(charger_labels[0]);

static void format_fault(char *text, size_t size, int fault) {
  if (fault < 0) {
    snprintf(text, size, "Read error");
    return;
  }
  if (fault == 0) {
    snprintf(text, size, "None");
    return;
  }
  const char *reason = fault & 0x80             ? "Watchdog"
                       : fault & 0x40           ? "Boost"
                       : (fault & 0x30) == 0x10 ? "Input"
                       : (fault & 0x30) == 0x20 ? "Thermal"
                       : (fault & 0x30) == 0x30 ? "Timer"
                       : fault & 0x08           ? "Battery"
                       : fault & 0x07           ? "Temperature"
                                                : "Unknown";
  // Keep the complete bitmask even if more than one fault is set.
  snprintf(text, size, "%s (0x%02X)", reason, fault);
}

static void format_charger_rows(const ChargeDiagnostics &status, char values[charger_row_count][32]) {
  for (size_t i = 0; i < charger_row_count; ++i)
    snprintf(values[i], 32, "%s", status.sample_ms ? "Read error" : "Reading...");
  if (!status.sample_ms)
    return;
  const int input = status.registers[0x00];
  const int config = status.registers[0x03];
  const int power = status.registers[0x0B];
  const int limits = status.registers[0x13];
  if (power >= 0) {
    static const char *const sources[] = {"None",         "USB host", "USB CDP",     "USB DCP",
                                          "High voltage", "Adapter",  "Nonstandard", "OTG"};
    snprintf(values[0], 32, "%s", power & 0x04 ? "Good" : "Not good");
    snprintf(values[1], 32, "%s", sources[(power >> 5) & 7]);
  }
  if (config >= 0)
    snprintf(values[2], 32, "%s", config & 0x10 ? "Yes" : "No");
  if (input >= 0 && config >= 0)
    snprintf(values[3], 32, "%s", input & 0x80 ? "Input off (HIZ)" : config & 0x20 ? "OTG" : "Normal");
  if (input >= 0)
    snprintf(values[4], 32, "%d mA", 100 + (input & 0x3F) * 50);
  if (limits >= 0)
    snprintf(values[5], 32, "%s",
             (limits & 0xC0) == 0xC0 ? "Voltage + current"
             : limits & 0x80         ? "Voltage"
             : limits & 0x40         ? "Current"
                                     : "None");
  format_fault(values[6], 32, status.fault_current);
  format_fault(values[7], 32, status.last_fault);
  snprintf(values[8], 32, "%lu s",
           (unsigned long)(((uint32_t)(esp_timer_get_time() / 1000) - status.sample_ms) / 1000));
}

void update_about_version_info() {
  if (!get_slint_window())
    return;

  char formattedString[128];
  snprintf(formattedString, sizeof(formattedString), "Version: %d.%d.%d.%s\nHW: %s\nHash: %s", VERSION_MAJOR,
           VERSION_MINOR, VERSION_PATCH, RELEASE_VARIANT, HW_TYPE, BUILD_ID);

  slint::SharedString version_info(formattedString);
  get_slint_window()->global<UiState>().set_version_info(version_info);
}

static void add_row(std::shared_ptr<slint::VectorModel<StatEntry>> &model, const char *label, const char *value) {
  StatEntry entry;
  entry.label = label;
  entry.value = value;
  entry.is_header = false;
  model->push_back(entry);
}

static void add_header(std::shared_ptr<slint::VectorModel<StatEntry>> &model, const char *label) {
  StatEntry entry;
  entry.label = label;
  entry.value = "";
  entry.is_header = true;
  model->push_back(entry);
}

static void update_row(size_t index, const char *value) {
  auto entry = stats_model->row_data(index).value();
  if (std::string_view(entry.value) != value) {
    entry.value = value;
    stats_model->set_row_data(index, entry);
  }
}

// Entry and the one-second UI timer both run on the Slint event loop.
extern "C" void update_about_stats() {
  if (!get_slint_window() || !is_about_screen_active())
    return;

  char voltage[16], level[16], internal_free[16], min_ever[16], largest[16], psram[16], current[16];
  const RemoteStats telemetry = stats_snapshot();
  snprintf(voltage, sizeof(voltage), "%.2f V", (float)telemetry.remoteBatteryVoltage / 1000.0f);
  snprintf(level, sizeof(level), "%d%%", telemetry.remoteBatteryPercentage);
  snprintf(current, sizeof(current), "%u mA", telemetry.chargeCurrent);
  snprintf(internal_free, sizeof(internal_free), "%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  snprintf(min_ever, sizeof(min_ever), "%u", (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  snprintf(largest, sizeof(largest), "%u", (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  snprintf(psram, sizeof(psram), "%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  const char *state = charge_state_to_string(telemetry.chargeState);
  bool show_current = telemetry.chargeState != CHARGE_STATE_NOT_CHARGING && telemetry.chargeCurrent > 0;
  ChargeDiagnostics charger;
  const bool show_charger = charge_driver_get_diagnostics(&charger);
  char charger_values[charger_row_count][32];
  if (show_charger)
    format_charger_rows(charger, charger_values);

  if (!stats_model) {
    stats_model = std::make_shared<slint::VectorModel<StatEntry>>();
    add_header(stats_model, "BATTERY");
    add_row(stats_model, "Voltage", voltage);
    add_row(stats_model, "Level", level);
    add_row(stats_model, "State", state);
    if (show_current)
      add_row(stats_model, "Current", current);
    add_header(stats_model, "MEMORY");
    add_row(stats_model, "Internal free", internal_free);
    add_row(stats_model, "Min ever", min_ever);
    add_row(stats_model, "Largest block", largest);
    add_row(stats_model, "PSRAM free", psram);
    stats_has_current = show_current;
    if (show_charger) {
      add_header(stats_model, charger.chip);
      for (size_t i = 0; i < charger_row_count; ++i)
        add_row(stats_model, charger_labels[i], charger_values[i]);
    }
    get_slint_window()->global<UiState>().set_about_stats(stats_model);
    return;
  }

  // Preserve the model and row instances; only charge-current visibility changes
  // the list structure. Memory counters can change on every refresh.
  bool had_current = stats_has_current;
  if (show_current && !had_current) {
    StatEntry entry;
    entry.label = "Current";
    entry.value = current;
    entry.is_header = false;
    stats_model->insert(4, entry);
  }
  else if (!show_current && had_current) {
    stats_model->erase(4);
  }
  stats_has_current = show_current;
  update_row(1, voltage);
  update_row(2, level);
  update_row(3, state);
  if (show_current)
    update_row(4, current);
  size_t memory_start = show_current ? 6 : 5;
  update_row(memory_start, internal_free);
  update_row(memory_start + 1, min_ever);
  update_row(memory_start + 2, largest);
  update_row(memory_start + 3, psram);
  if (show_charger) {
    const size_t charger_start = memory_start + 5;
    for (size_t i = 0; i < charger_row_count; ++i)
      update_row(charger_start + i, charger_values[i]);
  }
}

extern "C" void setup_about_properties() {
  update_about_version_info();
  update_about_stats();
}

extern "C" void teardown_about_properties() {
  stats_model.reset();
  stats_has_current = false;
}

// Slint event handlers
extern "C" void handle_about_back() {
  ESP_LOGI(TAG, "About back pressed");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Menu); });
}

extern "C" void handle_about_check_updates() {
  ESP_LOGI(TAG, "Check updates pressed");
  slint::invoke_from_event_loop([]() { get_slint_window()->global<UiState>().set_screen(Screen::Update); });
}
