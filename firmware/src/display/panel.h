#pragma once
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
// Private display integration: callers retain completion callback/context until teardown.
using PanelColorDone = bool (*)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *);
esp_err_t panel_init(PanelColorDone completed, void *context);
void panel_deinit(void);
esp_lcd_panel_handle_t panel_handle(void);
esp_lcd_touch_handle_t panel_touch_handle(void);
bool panel_is_initialized(void);
