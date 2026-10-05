#pragma once
#include <chrono>
#include <functional>
#include <string>
#include <vector>

struct UiState {
  mutable bool processing = false, dialog = true;
  mutable std::string message, error;
  void set_processing(bool value) const {
    processing = value;
  }
  void set_show_confirm_dialog(bool value) const {
    dialog = value;
  }
  void set_processing_message(const char *value) const {
    message = value;
  }
  void set_operation_error(const char *value) const {
    error = value;
  }
};
struct FakeWindow {
  UiState state;
  template <typename T> T &global() {
    return state;
  }
};
namespace slint
{
inline std::vector<std::function<void()>> events;
inline std::function<void()> delayed;
inline long long last_delay = 0;
enum class TimerMode {
  SingleShot
};
struct Timer {
  void start(TimerMode, std::chrono::milliseconds delay, std::function<void()> callback) {
    if (delay.count() < 1)
      throw "Timer must have a positive delay";
    last_delay = delay.count();
    delayed = std::move(callback);
  }
};
inline void invoke_from_event_loop(std::function<void()> callback) {
  events.push_back(std::move(callback));
}
} // namespace slint
