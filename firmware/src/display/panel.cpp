#include "display/panel.h"
#include "config.h"
#include "display/display_driver.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "remote/display.h"
#include "remote/i2c.h"
#if TP_CST816S
  #include "esp_lcd_touch_cst816s.h"
#elif TP_FT3168
  #include "esp_lcd_touch_ft5x06.h"
#elif TP_CST9217
  #include "esp_lcd_touch_cst9217.h"
#endif

#if DISP_GC9A01
  #include "esp_lcd_gc9a01.h"
  #define RGB_ELE_ORDER LCD_RGB_ELEMENT_ORDER_BGR
#elif DISP_SH8601 || DISP_CO5300
  #define SW_ROTATE 1
  #include "display/sh8601/display_driver_sh8601.h"
  #include "esp_lcd_sh8601.h"
  #define RGB_ELE_ORDER LCD_RGB_ELEMENT_ORDER_RGB
#elif DISP_ST7789
  #error "ST7789 not supported"
#endif

static const char *TAG = "PUBREMOTE-PANEL";
#define LCD_HOST SPI2_HOST
#define LCD_CMD_BITS 8
#define LCD_PARAM_BITS 8
#define MAX_TRAN_SIZE (HOR_RES * VER_RES * sizeof(uint16_t))

static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;
static bool is_initialized = false;
static uint8_t bl_level = 0;
static bool hbm_mode_active = false;

// esp_lcd panel IO is not thread safe, and brightness/HBM/sleep commands are issued from the IMU
// and power management tasks while the Slint task is mid-flush. Two concurrent
// spi_device_acquire_bus() calls on one device leave the bus lock held with no owner, and the
// next caller blocks in dev_wait() forever.
static SemaphoreHandle_t panel_io_lock = NULL;

extern "C" bool panel_io_lock_acquire(uint32_t timeout_ms) {
  if (!panel_io_lock) {
    return true;
  }
  if (xSemaphoreTakeRecursive(panel_io_lock, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    ESP_LOGW(TAG, "panel IO lock timed out; skipping panel access");
    return false;
  }
  return true;
}

extern "C" void panel_io_lock_release() {
  if (panel_io_lock) {
    xSemaphoreGiveRecursive(panel_io_lock);
  }
}

extern "C" uint8_t display_get_bl_level() {
  return bl_level;
}

extern "C" bool display_get_hbm() {
  return hbm_mode_active;
}

extern "C" void display_set_hbm(bool active) {
  if (active && !display_supports_hbm()) {
    return;
  }
  hbm_mode_active = active;
  if (is_initialized) {
    if (!panel_io_lock_acquire(1000)) {
      return;
    }
#if DISP_SH8601 || DISP_CO5300
    sh8601_set_hbm_mode(lcd_io, hbm_mode_active);
#endif
    if (hbm_mode_active) {
      set_display_brightness(lcd_io, 0);
    }
    else {
      set_display_brightness(lcd_io, bl_level);
    }
    panel_io_lock_release();
  }
}

extern "C" bool display_supports_hbm() {
#if DISP_SH8601 || DISP_CO5300
  return true;
#else
  return false;
#endif
}

extern "C" void display_set_bl_level(uint8_t level) {
  ESP_LOGI(TAG, "display_set_bl_level: %d (is_initialized: %d)", level, is_initialized);
  if (is_initialized) {
    bl_level = level;
    if (!hbm_mode_active) {
      if (!panel_io_lock_acquire(1000)) {
        return;
      }
      set_display_brightness(lcd_io, bl_level);
      panel_io_lock_release();
    }
  }
}

static esp_err_t app_lcd_init(PanelColorDone completed, void *context) {
  esp_err_t ret = ESP_OK;
  display_driver_preinit();
  ESP_LOGI(TAG, "Initialize SPI bus");

  spi_bus_config_t buscfg = {};
#if DISP_GC9A01
  buscfg.sclk_io_num = DISP_CLK;
  buscfg.mosi_io_num = DISP_MOSI;
  buscfg.miso_io_num = -1;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = MAX_TRAN_SIZE;
#elif DISP_SH8601 || DISP_CO5300
  buscfg.sclk_io_num = DISP_CLK;
  buscfg.data0_io_num = DISP_SDIO0;
  buscfg.data1_io_num = DISP_SDIO1;
  buscfg.data2_io_num = DISP_SDIO2;
  buscfg.data3_io_num = DISP_SDIO3;
  buscfg.max_transfer_sz = MAX_TRAN_SIZE;
#endif

  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
  ESP_LOGI(TAG, "Install panel IO");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#if DISP_GC9A01
  esp_lcd_panel_io_spi_config_t io_config =
      GC9A01_PANEL_IO_SPI_CONFIG((gpio_num_t)DISP_CS, (gpio_num_t)DISP_DC, completed, context);
  io_config.trans_queue_depth = 20;
#elif DISP_SH8601 || DISP_CO5300
  esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG((gpio_num_t)DISP_CS, completed, context);
  io_config.pclk_hz = LCD_PIXEL_CLOCK_HZ;
  io_config.trans_queue_depth = 20;
#endif
#pragma GCC diagnostic pop

  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &lcd_io));

#if DISP_GC9A01
  gc9a01_vendor_config_t vendor_config = {};
#elif DISP_SH8601 || DISP_CO5300
  sh8601_vendor_config_t vendor_config = {
  #if DISP_SH8601
      .init_cmds = sh8601_lcd_init_cmds,
      .init_cmds_size = (uint16_t)sh8601_get_lcd_init_cmds_size(),
  #elif DISP_CO5300
      .init_cmds = co5300_lcd_init_cmds,
      .init_cmds_size = (uint16_t)co5300_get_lcd_init_cmds_size(),
  #endif
      .flags =
          {
              .use_qspi_interface = 1,
          },
  };
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
  const esp_lcd_panel_dev_config_t panel_config = {
      .rgb_ele_order = RGB_ELE_ORDER,
      .bits_per_pixel = 16,
      .reset_gpio_num = (gpio_num_t)DISP_RST,
      .vendor_config = &vendor_config,
  };
#pragma GCC diagnostic pop

#if DISP_GC9A01
  ESP_LOGI(TAG, "Install GC9A01 panel driver");
  esp_err_t init_err = esp_lcd_new_panel_gc9a01(lcd_io, &panel_config, &lcd_panel);
#elif DISP_SH8601 || DISP_CO5300
  ESP_LOGI(TAG, "Install SH8601/CO5300 panel driver");
  esp_err_t init_err = esp_lcd_new_panel_sh8601(lcd_io, &panel_config, &lcd_panel);
#endif

  if (init_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install LCD driver");
    if (lcd_panel)
      esp_lcd_panel_del(lcd_panel);
    if (lcd_io)
      esp_lcd_panel_io_del(lcd_io);
    spi_bus_free(LCD_HOST);
    return ret;
  }

  ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel));
  bool invert_color = false;
  bool mirror_x = false;

#if DISP_GC9A01
  invert_color = true;
  mirror_x = true;
#elif DISP_SH8601 || DISP_CO5300
  invert_color = false;
  mirror_x = false;
#endif

#ifdef PANEL_X_GAP
  uint8_t panel_x_gap = PANEL_X_GAP;
#else
  uint8_t panel_x_gap = 0;
#endif

#ifdef PANEL_Y_GAP
  uint8_t panel_y_gap = PANEL_Y_GAP;
#else
  uint8_t panel_y_gap = 0;
#endif

  if (panel_x_gap > 0 || panel_y_gap > 0) {
    esp_lcd_panel_set_gap(lcd_panel, panel_x_gap, panel_y_gap);
  }

  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(lcd_panel, invert_color));
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(lcd_panel, mirror_x, false));
  esp_lcd_panel_disp_on_off(lcd_panel, true);
  ESP_ERROR_CHECK(test_display_communication(lcd_io));

  return ret;
}

#if TOUCH_ENABLED
static esp_err_t app_touch_init(void) {
  esp_lcd_panel_io_handle_t tp_io_handle = NULL;

  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
  #if TP_CST816S
  esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
  #elif TP_FT3168
  esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
  #elif TP_CST9217
  esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
  #endif

  tp_io_config.scl_speed_hz = I2C_SCL_FREQ_HZ;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_get_bus_handle(), &tp_io_config, &tp_io_handle));

  const esp_lcd_touch_config_t tp_cfg = {
      .x_max = HOR_RES,
      .y_max = VER_RES,
      .rst_gpio_num = (gpio_num_t)TP_RST,
  // Only the CST816S and FT5x06 drivers configure the INT pin for interrupts;
  // the Slint event loop switches from polling to interrupt-driven touch when
  // int_gpio_num is set. CST9217 never sets up the GPIO, so leave it NC there.
  #if defined(TP_INT) && (defined(TP_CST816S) || defined(TP_FT3168))
      .int_gpio_num = (gpio_num_t)TP_INT,
  #else
      .int_gpio_num = GPIO_NUM_NC,
  #endif
      .flags =
          {
              .swap_xy = 0,
              .mirror_x = 0,
              .mirror_y = 0,
          },
  };
  #pragma GCC diagnostic pop

  #if TP_CST816S
  ESP_LOGI(TAG, "Initialize touch controller CST816S");
  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &touch_handle));
  #elif TP_FT3168
  ESP_LOGI(TAG, "Initialize touch controller FT3168");
  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &touch_handle));
  #elif TP_CST9217
  ESP_LOGI(TAG, "Initialize touch controller CST9217");
  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst9217(tp_io_handle, &tp_cfg, &touch_handle));
  #endif

  return ESP_OK;
}
#endif

extern "C" void display_off() {
  ESP_LOGI(TAG, "Display sleep");
  if (lcd_panel && panel_io_lock_acquire(1000)) {
    esp_lcd_panel_disp_on_off(lcd_panel, false);
    esp_lcd_panel_disp_sleep(lcd_panel, true);
    panel_io_lock_release();
  }
  if (touch_handle) {
    esp_lcd_touch_enter_sleep(touch_handle);
  }
}

esp_err_t panel_init(PanelColorDone completed, void *context) {
  if (!panel_io_lock)
    panel_io_lock = xSemaphoreCreateRecursiveMutex();
  ESP_ERROR_CHECK(app_lcd_init(completed, context));
#if TOUCH_ENABLED
  ESP_ERROR_CHECK(app_touch_init());
#endif
  is_initialized = true;
  return ESP_OK;
}
void panel_deinit(void) {
  if (touch_handle) {
    esp_lcd_touch_del(touch_handle);
    touch_handle = NULL;
  }
  bool panel_locked = panel_io_lock_acquire(1000);
  if (lcd_panel) {
    esp_lcd_panel_del(lcd_panel);
    lcd_panel = NULL;
  }
  if (lcd_io) {
    esp_lcd_panel_io_del(lcd_io);
    lcd_io = NULL;
  }
  spi_bus_free(LCD_HOST);
  if (panel_locked) {
    panel_io_lock_release();
  }

  is_initialized = false;
}
esp_lcd_panel_handle_t panel_handle(void) {
  return lcd_panel;
}
esp_lcd_touch_handle_t panel_touch_handle(void) {
  return touch_handle;
}
bool panel_is_initialized(void) {
  return is_initialized;
}
