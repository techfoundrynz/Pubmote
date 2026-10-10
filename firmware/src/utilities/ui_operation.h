#pragma once
#include <esp_err.h>
#include <functional>

// UI-thread entry points. The work function must not access Slint; completion
// runs on the UI thread after the modal processing overlay has been dismissed.
bool ui_operation_start(const char *message, std::function<esp_err_t()> work, std::function<void()> completed = {});
bool ui_processing_begin(const char *message);
// Keep feedback visible for at least 500 ms; completion runs after dismissal.
void ui_processing_end(std::function<void()> completed = {});
bool ui_processing_active();
// For quick preference toggles whose change is already on screen: saves on the
// same worker as ui_operation_start but without the overlay. Repeats within the
// debounce collapse into one write; only a failure is shown.
void ui_save_quietly(std::function<esp_err_t()> work);
// Run a pending quiet save now instead of waiting out the debounce.
void ui_save_quietly_flush();
void ui_restart();
