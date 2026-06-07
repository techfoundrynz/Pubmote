#include "display_driver_sh8601.h"
#include "driver/ledc.h"
#include <esp_lcd_panel_io.h>
#include <esp_lcd_sh8601.h>
#include <esp_log.h>

static const char *TAG = "PUBREMOTE-SH8601";

// Adapted from display_driver_sh8601.c
#define LCD_OPCODE_WRITE_CMD (0x02ULL)
#define LCD_OPCODE_READ_CMD (0x03ULL)
#define LCD_OPCODE_WRITE_COLOR (0x32ULL)
#define USE_QSPI_INTERFACE 1

// https://github.com/nishad2m8/T-Display-S3-DS-1.43-YT/blob/master/01-Auto-Gauge/PIO/lib/Arduino_GFX-1.3.7/src/display/Arduino_SH8601.h
const sh8601_lcd_init_cmd_t sh8601_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {SH8601_C_SLPOUT, (uint8_t[]){0x00}, 0, SH8601_SLPOUT_DELAY},
    {SH8601_C_NORON, (uint8_t[]){0x00}, 0, 0},
    {SH8601_C_INVOFF, (uint8_t[]){0x00}, 0, 0},
    {SH8601_W_PIXFMT, (uint8_t[]){0x05}, 1, 0}, // Interface Pixel Format 16bit/pixel
    {SH8601_C_DISPON, (uint8_t[]){0x00}, 0, 0},
    {SH8601_W_WCTRLD1, (uint8_t[]){0x28}, 1, 0},            // Brightness Control On and Display Dimming On
    {SH8601_W_WDBRIGHTNESSVALNOR, (uint8_t[]){0x00}, 1, 0}, // Brightness adjustment
    {SH8601_W_WCE, (uint8_t[]){0x00}, 1, 10},               // High contrast mode (Sunlight Readability Enhancement)
};

size_t sh8601_get_lcd_init_cmds_size(void) {
  return sizeof(sh8601_lcd_init_cmds) / sizeof(sh8601_lcd_init_cmds[0]);
}

// Reference:
// https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.43/ESP32-S3-AMOLED-1.43-Demo-V3.zip
// https://files.waveshare.com/wiki/common/CO5300(ICNA3311)_Datasheet_V0.01_20230724.pdf
const sh8601_lcd_init_cmd_t co5300_lcd_init_cmds[] = {
    {SH8601_C_SLPOUT, (uint8_t[]){0x00}, 0, 80},
    {SH8601_W_SPIMODECTL, (uint8_t[]){0x80}, 1, 0},
    //{SH8601_W_SETTSL, (uint8_t []){0x01, 0xD1}, 2, 0},
    //{SH8601_WC_TEARON, (uint8_t []){0x00}, 1, 0},//TE ON
    {SH8601_W_WCTRLD1, (uint8_t[]){0x20}, 1, 1},
    {SH8601_W_WDBRIGHTNESSVALHBM, (uint8_t[]){0xFF}, 1, 1},
    {SH8601_W_WDBRIGHTNESSVALNOR, (uint8_t[]){0x00}, 1, 1},
    {SH8601_C_DISPON, (uint8_t[]){0x00}, 0, 10},
    {SH8601_W_WDBRIGHTNESSVALNOR, (uint8_t[]){0xFF}, 1, 0},
    //{SH8601_W_MADCTL, (uint8_t []){0x60}, 1, 0},
};

size_t co5300_get_lcd_init_cmds_size(void) {
  return sizeof(co5300_lcd_init_cmds) / sizeof(co5300_lcd_init_cmds[0]);
}

static esp_err_t tx_param(esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *param, size_t param_size) {
  if (USE_QSPI_INTERFACE) {
    lcd_cmd &= 0xff;
    lcd_cmd <<= 8;
    lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;
  }
  return esp_lcd_panel_io_tx_param(io, lcd_cmd, param, param_size);
}

static esp_err_t rx_param(esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *param, size_t param_size) {
  if (USE_QSPI_INTERFACE) {
    lcd_cmd &= 0xff;
    lcd_cmd <<= 8;
    lcd_cmd |= LCD_OPCODE_READ_CMD << 24;
  }
  return esp_lcd_panel_io_tx_param(io, lcd_cmd, param, param_size);
}

esp_err_t sh8601_test_display_communication(esp_lcd_panel_io_handle_t io_handle) {
  ESP_LOGI(TAG, "Testing SH8601Z communication");

  uint8_t id_array[3] = {0};

  rx_param(io_handle, SH8601_R_RDID, &id_array, 3);

  ESP_LOGI(TAG, "Display ID: %02X %02X %02X", id_array[0], id_array[1], id_array[2]);
  return ESP_OK;
}

esp_err_t sh8601_display_driver_preinit() {
  ESP_LOGI(TAG, "Preinit SH8601 display driver");
  gpio_reset_pin(DISP_BL);
  gpio_set_direction(DISP_BL, GPIO_MODE_OUTPUT);
  ESP_ERROR_CHECK(gpio_set_level(DISP_BL, 1));
  return ESP_OK;
}

esp_err_t sh8601_set_display_brightness(esp_lcd_panel_io_handle_t io_handle, uint8_t brightness) {
  // Create a buffer for the command and brightness value
  uint8_t data[1] = {brightness};

  // Send the command and brightness value over SPI
  return tx_param(io_handle, SH8601_W_WDBRIGHTNESSVALNOR, data, 1);
}

esp_err_t sh8601_set_hbm_mode(esp_lcd_panel_io_handle_t io_handle, bool hbm_on) {
  uint8_t ctl_val[1] = { (uint8_t)(hbm_on ? 0xFF : 0x00) };
  return tx_param(io_handle, SH8601_W_WHBMCTL, ctl_val, 1);
}
