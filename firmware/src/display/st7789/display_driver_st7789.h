#ifndef __DISPLAY_DRIVER_ST7789_H
#define __DISPLAY_DRIVER_ST7789_H
#include <esp_err.h>
#include <esp_lcd_types.h>

#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
esp_err_t st7789_test_display_communication(esp_lcd_panel_io_handle_t io_handle);
esp_err_t st7789_display_driver_preinit();
esp_err_t st7789_set_display_brightness(esp_lcd_panel_io_handle_t io_handle, uint8_t brightness);

#endif