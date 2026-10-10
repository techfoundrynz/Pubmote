#include "remote/radio_session.h"
#include "ui/slint_window.h"
#include "utilities/ui_operation.h"
#include <cassert>
#include <iostream>
#include <new>
#include <thread>

static FakeWindow window;
static int sleep_resets, reboots;
static bool worker_thread, fail_allocation;
static esp_err_t submit_result;
static radio_session_work_t pending_work;
static radio_session_callback_t pending_done;
static void *pending_context;

void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
  if (fail_allocation) {
    fail_allocation = false;
    return nullptr;
  }
  try {
    return ::operator new(size);
  }
  catch (...) {
    return nullptr;
  }
}
void operator delete(void *ptr, const std::nothrow_t &) noexcept {
  ::operator delete(ptr);
}
FakeWindow *get_slint_window() {
  return &window;
}
void reset_sleep_timer() {
  ++sleep_resets;
}
void esp_restart() {
  assert(!worker_thread && !ui_processing_active());
  ++reboots;
}
esp_err_t radio_session_run(radio_session_work_t work, radio_session_callback_t done, void *context) {
  assert(!worker_thread && window.state.processing && !pending_work);
  if (submit_result != ESP_OK)
    return submit_result;
  pending_work = work;
  pending_done = done;
  pending_context = context;
  return ESP_OK;
}
static void paint_then_dispatch() {
  assert(window.state.processing && slint::delayed);
  auto dispatch = std::move(slint::delayed);
  slint::delayed = {};
  dispatch();
}
static void run_worker() {
  assert(pending_work);
  worker_thread = true;
  auto work = pending_work;
  pending_work = nullptr;
  auto result = work(pending_context);
  pending_done(result, pending_context);
  worker_thread = false;
}
static void dismiss_overlay() {
  assert(ui_processing_active() && window.state.processing && slint::delayed);
  assert(slint::last_delay > 0 && slint::last_delay <= 500);
  auto dismiss = std::move(slint::delayed);
  slint::delayed = {};
  dismiss();
}
static void run_ui() {
  auto events = std::move(slint::events);
  slint::events.clear();
  for (auto &event : events)
    event();
}

static bool rx_running, tx_running, manager_running;
static std::string reset_failure, reset_calls;
static esp_err_t reset_step(const char *step, bool *running = nullptr, bool start = false) {
  reset_calls += step;
  reset_calls += " ";
  if (reset_failure == step)
    return ESP_ERR_TIMEOUT;
  if (running)
    *running = start;
  return ESP_OK;
}
static esp_err_t connection_deinit() {
  return reset_step("stop-manager", &manager_running);
}
static esp_err_t transmitter_deinit() {
  return reset_step("stop-tx", &tx_running);
}
static esp_err_t receiver_deinit() {
  return reset_step("stop-rx", &rx_running);
}
static esp_err_t receiver_start() {
  return reset_step("start-rx", &rx_running, true);
}
static esp_err_t transmitter_start() {
  return reset_step("start-tx", &tx_running, true);
}
static esp_err_t connection_start() {
  return reset_step("start-manager", &manager_running, true);
}
static esp_err_t reset_all_settings() {
  assert(!manager_running && !rx_running && !tx_running);
  return reset_step("erase");
}
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGE(...) ((void)0)
static void vTaskDelay(int ms) {
  assert(ms == 20);
}
#include "radio_reset_functions.inc"

int main() {
  int work_calls = 0, completions = 0;
  assert(ui_operation_start(
      "Switching radio...",
      [&]() {
        assert(worker_thread && ui_processing_active());
        ++work_calls;
        return ESP_OK;
      },
      [&]() {
        assert(!worker_thread && !ui_processing_active() && !window.state.processing);
        ++completions;
      }));
  assert(ui_processing_active() && window.state.message == "Switching radio..." && !window.state.dialog);
  assert(!ui_operation_start("Duplicate", []() {
    assert(false);
    return ESP_OK;
  }));
  assert(work_calls == 0 && !pending_work && sleep_resets == 1);
  paint_then_dispatch();
  assert(work_calls == 0);
  run_worker();
  assert(work_calls == 1 && completions == 0 && ui_processing_active());
  run_ui();
  assert(completions == 0 && ui_processing_active() && window.state.processing);
  assert(slint::last_delay >= 400); // A quick operation still waits out the 500 ms minimum.
  dismiss_overlay();
  assert(completions == 1 && !ui_processing_active() && window.state.error.empty());

  assert(ui_operation_start("Saving...", []() { return ESP_ERR_TIMEOUT; }, [&]() { assert(false); }));
  paint_then_dispatch();
  run_worker();
  run_ui();
  dismiss_overlay();
  assert(!ui_processing_active() && !window.state.error.empty());

  submit_result = ESP_ERR_TIMEOUT;
  assert(ui_operation_start("Queued...", []() {
    assert(false);
    return ESP_OK;
  }));
  assert(window.state.error.empty());
  paint_then_dispatch();
  run_ui();
  dismiss_overlay();
  assert(!ui_processing_active() && !pending_work && !window.state.error.empty());
  submit_result = ESP_OK;

  fail_allocation = true;
  assert(!ui_operation_start("Allocation...", []() {
    assert(false);
    return ESP_OK;
  }));
  run_ui();
  dismiss_overlay();
  assert(!ui_processing_active() && !window.state.error.empty());

  ui_restart();
  assert(window.state.processing && window.state.message == "Restarting..." && reboots == 0);
  paint_then_dispatch();
  assert(reboots == 0);
  run_worker();
  run_ui();
  assert(reboots == 0 && ui_processing_active());
  dismiss_overlay();
  assert(reboots == 1 && !ui_processing_active());

  assert(ui_operation_start("Slow...", []() {
    std::this_thread::sleep_for(std::chrono::milliseconds(510));
    return ESP_OK;
  }));
  paint_then_dispatch();
  run_worker();
  run_ui();
  assert(!ui_processing_active() && !slint::delayed);
  rx_running = tx_running = manager_running = true;
  assert(radio_session_reset_settings() == ESP_OK);
  assert(reset_calls == "stop-manager stop-tx stop-rx erase ");
  for (const char *step : {"stop-manager", "stop-tx", "stop-rx", "erase"}) {
    reset_failure = step;
    reset_calls.clear();
    rx_running = tx_running = manager_running = true;
    assert(radio_session_reset_settings() == ESP_ERR_TIMEOUT);
    if (reset_failure != "erase")
      assert(reset_calls.find("erase") == std::string::npos);
    assert(rx_running && tx_running && manager_running);
  }
  std::cout << "Processing feedback, worker separation, duplicate rejection and failure recovery passed\n";
}
