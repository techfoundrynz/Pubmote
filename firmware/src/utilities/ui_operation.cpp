#include "utilities/ui_operation.h"
#include "esp_system.h"
#include "remote/powermanagement.h"
#include "remote/radio_session.h"
#include "slint_generated/app-window.h"
#include "ui/slint_window.h"
#include <atomic>
#include <new>

namespace
{
std::atomic<bool> busy{false};
slint::Timer dispatch_timer;
slint::Timer dismiss_timer;
std::chrono::steady_clock::time_point shown_at;
struct Job {
  std::function<esp_err_t()> work;
  std::function<void()> completed;
};
// Quiet saves, UI thread only. The latest request wins; at most one is queued.
slint::Timer quiet_timer;
std::function<esp_err_t()> quiet_work;
bool quiet_in_flight = false;
void quiet_dispatch();
void quiet_done(esp_err_t result, void *context) {
  auto *work = static_cast<std::function<esp_err_t()> *>(context);
  slint::invoke_from_event_loop([result, work]() {
    delete work;
    quiet_in_flight = false;
    if (result != ESP_OK)
      get_slint_window()->global<UiState>().set_operation_error("The setting could not be saved. Please try again.");
    // Tapped again while saving and the debounce has already expired
    if (quiet_work && !quiet_timer.running())
      quiet_dispatch();
  });
}
void quiet_dispatch() {
  if (quiet_in_flight || !quiet_work)
    return;
  auto *work = new (std::nothrow) std::function<esp_err_t()>(std::move(quiet_work));
  quiet_work = nullptr;
  if (!work) {
    get_slint_window()->global<UiState>().set_operation_error("The setting could not be saved. Please try again.");
    return;
  }
  quiet_in_flight = true;
  esp_err_t result = radio_session_run(
      [](void *context) { return (*static_cast<std::function<esp_err_t()> *>(context))(); }, quiet_done, work);
  if (result != ESP_OK)
    quiet_done(result, work);
}
void finish(esp_err_t result, void *context) {
  auto *job = static_cast<Job *>(context);
  slint::invoke_from_event_loop([result, job]() {
    ui_processing_end([result, job]() {
      if (result == ESP_OK) {
        if (job->completed)
          job->completed();
      }
      else {
        get_slint_window()->global<UiState>().set_operation_error("The operation could not finish. Please try again.");
      }
      delete job;
    });
  });
}
} // namespace
bool ui_processing_active() {
  return busy.load();
}
bool ui_processing_begin(const char *message) {
  if (busy.exchange(true))
    return false;
  shown_at = std::chrono::steady_clock::now();
  reset_sleep_timer();
  const auto &state = get_slint_window()->global<UiState>();
  state.set_show_confirm_dialog(false);
  state.set_operation_error("");
  state.set_processing_message(message);
  state.set_processing(true);
  return true;
}
void ui_processing_end(std::function<void()> completed) {
  auto dismiss = [completed = std::move(completed)]() {
    busy = false;
    get_slint_window()->global<UiState>().set_processing(false);
    if (completed)
      completed();
  };
  const auto elapsed = std::chrono::steady_clock::now() - shown_at;
  const auto minimum = std::chrono::milliseconds(500);
  if (busy && elapsed < minimum) {
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(minimum - elapsed);
    dismiss_timer.start(slint::TimerMode::SingleShot, remaining, std::move(dismiss));
  }
  else
    dismiss();
}
bool ui_operation_start(const char *message, std::function<esp_err_t()> work, std::function<void()> completed) {
  if (!ui_processing_begin(message))
    return false;
  // Queue any pending quiet save first; the worker runs in order, so it can't
  // land after a reset or be lost to a shutdown.
  ui_save_quietly_flush();
  auto *job = new (std::nothrow) Job{std::move(work), std::move(completed)};
  if (!job) {
    finish(ESP_ERR_NO_MEM, nullptr);
    return false;
  }
  // Let the overlay reach the panel before flash/radio work begins.
  dispatch_timer.start(slint::TimerMode::SingleShot, std::chrono::milliseconds(100), [job]() {
    esp_err_t result =
        radio_session_run([](void *context) { return static_cast<Job *>(context)->work(); }, finish, job);
    if (result != ESP_OK)
      finish(result, job);
  });
  return true;
}
void ui_save_quietly(std::function<esp_err_t()> work) {
  quiet_work = std::move(work);
  quiet_timer.start(slint::TimerMode::SingleShot, std::chrono::milliseconds(750), quiet_dispatch);
}
void ui_save_quietly_flush() {
  quiet_timer.stop();
  quiet_dispatch();
}
void ui_restart() {
  ui_operation_start("Restarting...", []() { return ESP_OK; }, []() { esp_restart(); });
}
