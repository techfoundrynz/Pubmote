#ifndef __DISPLAY_DRIVER_GC9A01_H
#define __DISPLAY_DRIVER_GC9A01_H
#include <esp_err.h>
#include <esp_lcd_types.h>


#ifdef __cplusplus
extern "C" {
#endif


#define LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)
esp_err_t gc9a01_test_display_communication(esp_lcd_panel_io_handle_t io_handle);
esp_err_t gc9a01_display_driver_preinit();
esp_err_t gc9a01_set_display_brightness(esp_lcd_panel_io_handle_t io_handle, uint8_t brightness);



#ifdef __cplusplus
}
#endif

#endif