#pragma once

#include "esp_err.h"
#include "settings_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  void display_init();
  void display_deinit();
  uint8_t display_get_bl_level();
  void display_set_bl_level(uint8_t level);
  void display_set_rotation(ScreenRotation rot);
  void display_off();
  bool display_get_hbm();
  void display_set_hbm(bool active);
  bool display_supports_hbm();

  // Short label for a mode, for display in the UI.
  const char *hbm_mode_label(HbmModeOptions mode);
  void display_set_joystick_supported(bool supported);

  void apply_theme_settings();

  void display_refresh_device_settings(const DeviceSettings *previous);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
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
#endif
