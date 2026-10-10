#pragma once

// UI task only. Workers must dispatch through slint::invoke_from_event_loop.
#include "slint_generated/app-window.h"
#include <optional>

class SlintWindowPtr {
  std::optional<slint::ComponentHandle<AppWindow>> handle;

public:
  SlintWindowPtr() = default;
  SlintWindowPtr(std::optional<slint::ComponentHandle<AppWindow>> h) : handle(h) {
  }
  SlintWindowPtr(slint::ComponentHandle<AppWindow> h) : handle(h) {
  }

  SlintWindowPtr &operator=(slint::ComponentHandle<AppWindow> h) {
    handle = h;
    return *this;
  }

  void reset() {
    handle.reset();
  }

  operator bool() const {
    return handle.has_value();
  }
  bool operator!() const {
    return !handle.has_value();
  }

  AppWindow *operator->() const {
    return const_cast<AppWindow *>(handle.value().operator->());
  }
  AppWindow &operator*() const {
    return const_cast<AppWindow &>(*handle.value());
  }
};

AppWindow *get_slint_window();