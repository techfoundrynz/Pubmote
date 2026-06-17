#ifndef __DISPLAY_H
#define __DISPLAY_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SCREEN_ROTATION_0,
  SCREEN_ROTATION_90,
  SCREEN_ROTATION_180,
  SCREEN_ROTATION_270,
} ScreenRotation;

void display_init();
void display_deinit();
uint8_t display_get_bl_level();
void display_set_bl_level(uint8_t level);
void display_set_rotation(ScreenRotation rot);
void display_off();
bool display_get_hbm();
void display_set_hbm(bool active);
bool display_supports_hbm();

void apply_theme_settings();

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <optional>
#include "generated/app-window.h"

class SlintWindowPtr {
    std::optional<slint::ComponentHandle<AppWindow>> handle;
public:
    SlintWindowPtr() = default;
    SlintWindowPtr(std::optional<slint::ComponentHandle<AppWindow>> h) : handle(h) {}
    SlintWindowPtr(slint::ComponentHandle<AppWindow> h) : handle(h) {}
    
    SlintWindowPtr& operator=(slint::ComponentHandle<AppWindow> h) {
        handle = h;
        return *this;
    }
    
    void reset() { handle.reset(); }
    
    operator bool() const { return handle.has_value(); }
    bool operator!() const { return !handle.has_value(); }
    
    AppWindow* operator->() const {
        return const_cast<AppWindow*>(handle.value().operator->());
    }
    AppWindow& operator*() const {
        return const_cast<AppWindow&>(*handle.value());
    }
};

SlintWindowPtr get_slint_window();
#endif

#endif