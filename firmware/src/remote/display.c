#include "display.h"
#include "config.h"
#include "display/display_driver.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/ledc_types.h"
#include "lvgl.h"
#include "powermanagement.h"
#include "remote/i2c.h"
#include "settings.h"
#include "ui/ui.h"
#include "utilities/screen_utils.h"
#include "utilities/theme_utils.h"
#include <stdio.h>

// Display configuration
#if TP_CST816S
  #include "esp_lcd_touch_cst816s.h"
#elif TP_FT3168
  #include "esp_lcd_touch_ft3168.h"
#elif TP_CST9217
  #include "esp_lcd_touch_cst9217.h"
#endif

// https://github.com/espressif/esp-idf/blob/master/examples/peripherals/lcd/spi_lcd_touch/main/spi_lcd_touch_example_main.c
// https://github.com/espressif/esp-bsp/tree/master/components/lcd/esp_lcd_gc9a01
// I2C touch controller
// https://github.com/krupis/T-Display-S3-esp-idf/blob/923162ab67efe6f867ab1a3cdce19fe127c5c493/components/lvgl_setup/lvgl_setup.c#L255
// https://github.com/espressif/esp-iot-solution/tree/1f855ac00e3fdb33773c8ba5893d4f98f1d9b576/components/display/lcd/esp_lcd_sh8601#if
// https://github.com/nikthefix/Lilygo_Support_T_Encoder_Pro_Smart_Watch/blob/075f7020b162513062f9bf7415c1b4fa52a8673d/sls_encoder_pro_watch/sh8601.cpp#L48

#if DISP_GC9A01
  #include "esp_lcd_gc9a01.h"
  #define RGB_ELE_ORDER LCD_RGB_ELEMENT_ORDER_BGR
#elif DISP_SH8601 || DISP_CO5300
  #define SW_ROTATE 1
  #define ROUNDER_CALLBACK 1
  #include "display/sh8601/display_driver_sh8601.h"
  #include "esp_lcd_sh8601.h"
  #define RGB_ELE_ORDER LCD_RGB_ELEMENT_ORDER_RGB
#elif DISP_ST7789
  #error "ST7789 not supported"
#endif

#include "remoteinputs.h"

static const char *TAG = "PUBREMOTE-DISPLAY";

#define LCD_HOST SPI2_HOST

// Bit number used to represent command and parameter
#define LCD_CMD_BITS 8
#define LCD_PARAM_BITS 8

// LVGL
#define LVGL_TICK_PERIOD_MS 5
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_CPU_AFFINITY (portNUM_PROCESSORS - 1)
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY 20
#define BUFFER_LINES ((int)(LV_VER_RES / 10))
#define BUFFER_SIZE (LV_HOR_RES * BUFFER_LINES)
#define MAX_TRAN_SIZE ((int)LV_HOR_RES * BUFFER_LINES * sizeof(uint16_t))

#define SCREEN_TEST_UI 0

/* LCD IO and panel */
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
#if TOUCH_ENABLED
typedef void (*lv_indev_read_cb_t)(lv_indev_t *indev, lv_indev_data_t *data);
static lv_indev_read_cb_t original_read_cb = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;
#endif

/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;
#if TOUCH_ENABLED
static lv_indev_t *lvgl_touch_indev = NULL;
#endif

static lv_indev_t *lvgl_encoder_indev = NULL;

/* LCD panel gap */

#ifdef PANEL_X_GAP
static uint8_t panel_x_gap = PANEL_X_GAP;
#else
static uint8_t panel_x_gap = 0;
#endif

#ifdef PANEL_Y_GAP
static uint8_t panel_y_gap = PANEL_Y_GAP;
#else
static uint8_t panel_y_gap = 0;
#endif

static bool is_initialized = false;

#if ROUNDER_CALLBACK
void LVGL_port_rounder_callback(lv_event_t *e) {
  lv_area_t *area = lv_event_get_param(e);
  if (area == NULL) {
    ESP_LOGI(TAG, "Area is NULL in rounder callback");
    return;
  }

  uint16_t x1 = area->x1;
  uint16_t x2 = area->x2;
  uint16_t y1 = area->y1;
  uint16_t y2 = area->y2;

  // round the start of area down to the nearest even number
  area->x1 = (x1 >> 1) << 1;
  area->y1 = (y1 >> 1) << 1;

  // round the end of area up to the nearest odd number
  area->x2 = ((x2 >> 1) << 1) + 1;
  area->y2 = ((y2 >> 1) << 1) + 1;
}
#endif

static void encoder_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  static int32_t last_encoder_value = 0;
  static uint32_t last_time = 0;
  uint32_t current_time = lv_tick_get();
  int16_t enc_diff = 0;
  data->state = remote_data.bt_c ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

  if (remote_data.js_y > 0.5) {
    enc_diff = -1;
  }
  else if (remote_data.js_y < -0.5) {
    enc_diff = 1;
  }

  // Apply throttling ONLY to encoder movement
  if (last_encoder_value == enc_diff || (current_time - last_time) < 100) {
    data->enc_diff = 0;
    return;
  }

  data->enc_diff = enc_diff;
  // Update tracking variables
  last_encoder_value = enc_diff;
  last_time = current_time;
}

bool LVGL_lock(int timeout_ms) {
  if (!lv_is_initialized()) {
    return false;
  }

  return lvgl_port_lock(timeout_ms);
}

void LVGL_unlock(void) {
  if (!lv_is_initialized()) {
    return;
  }

  lvgl_port_unlock();
}

static uint8_t bl_level = 0;

uint8_t display_get_bl_level() {
  return bl_level;
}

void display_set_bl_level(uint8_t level) {
  if (is_initialized) {
    bl_level = level;
    set_display_brightness(lcd_io, bl_level);
  }
}

static esp_err_t app_lcd_init(void) {
  esp_err_t ret = ESP_OK;
  display_driver_preinit();
  ESP_LOGI(TAG, "Initialize SPI bus");

#if DISP_GC9A01
  const spi_bus_config_t buscfg = GC9A01_PANEL_BUS_SPI_CONFIG(DISP_CLK, DISP_MOSI, MAX_TRAN_SIZE);
#elif DISP_SH8601 || DISP_CO5300
  const spi_bus_config_t buscfg =
      SH8601_PANEL_BUS_QSPI_CONFIG(DISP_CLK, DISP_SDIO0, DISP_SDIO1, DISP_SDIO2, DISP_SDIO3, MAX_TRAN_SIZE);
#elif DISP_ST7789
  #error "ST7789 not supported"
#endif

  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
  ESP_LOGI(TAG, "Install panel IO");

#if DISP_GC9A01
  const esp_lcd_panel_io_spi_config_t io_config = GC9A01_PANEL_IO_SPI_CONFIG(DISP_CS, DISP_DC, NULL, NULL);
#elif DISP_SH8601 || DISP_CO5300
  const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(DISP_CS, NULL, NULL);
#elif DISP_ST7789
  #error "ST7789 not supported"
#endif

  // Attach the LCD to the SPI bus
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &lcd_io));

#if DISP_GC9A01
  gc9a01_vendor_config_t vendor_config = {};
#elif DISP_SH8601 || DISP_CO5300
  sh8601_vendor_config_t vendor_config = {
  #if DISP_SH8601
      .init_cmds = sh8601_lcd_init_cmds,
      .init_cmds_size = sh8601_get_lcd_init_cmds_size(),
  #elif DISP_CO5300
      .init_cmds = co5300_lcd_init_cmds,
      .init_cmds_size = co5300_get_lcd_init_cmds_size(),
  #endif
      .flags =
          {
              .use_qspi_interface = 1,
          },
  };
#elif DISP_ST7789
  #error "ST7789 not supported"
#endif

  const esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = DISP_RST,
      .rgb_ele_order = RGB_ELE_ORDER,
      .bits_per_pixel = LV_COLOR_DEPTH,
      .vendor_config = &vendor_config,
  };

#if DISP_GC9A01
  ESP_LOGI(TAG, "Install GC9A01 panel driver");
  esp_err_t init_err = esp_lcd_new_panel_gc9a01(lcd_io, &panel_config, &lcd_panel);
#elif DISP_SH8601 || DISP_CO5300
  ESP_LOGI(TAG, "Install SH8601/CO5300 panel driver");
  esp_err_t init_err = esp_lcd_new_panel_sh8601(lcd_io, &panel_config, &lcd_panel);
#elif DISP_ST7789
  #error "ST7789 not supported"
#endif

  if (init_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install LCD driver");

    if (lcd_panel) {
      esp_lcd_panel_del(lcd_panel);
    }

    if (lcd_io) {
      esp_lcd_panel_io_del(lcd_io);
    }

    spi_bus_free(LCD_HOST);
    return ret;
  }

  ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel));
  bool invert_color = false;

#if DISP_GC9A01
  invert_color = true;
#elif DISP_SH8601 || DISP_CO5300
  invert_color = false;
#elif DISP_ST7789
  #error "ST7789 not supported"
#endif

  // Set lcd panel gap
  ESP_LOGI(TAG, "panel_x_gap: %d", panel_x_gap);
  ESP_LOGI(TAG, "panel_y_gap: %d", panel_y_gap);

  if (panel_x_gap > 0 || panel_y_gap > 0) {
    esp_lcd_panel_set_gap(lcd_panel, panel_x_gap, panel_y_gap);
  }

  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(lcd_panel, invert_color));
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(lcd_panel, true, false));
  esp_lcd_panel_disp_on_off(lcd_panel, true);
  ESP_LOGI(TAG, "Test display communication");
  ESP_ERROR_CHECK(test_display_communication(lcd_io));

  return ret;
}

#if TOUCH_ENABLED
static esp_err_t app_touch_init(void) {
  // I2C bus should already be initialized at this point

  esp_lcd_panel_io_handle_t tp_io_handle = NULL;

  #if TP_CST816S
  esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
  #elif TP_FT3168
  esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT3168_CONFIG();
  #elif TP_CST9217
  esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
  #endif

  tp_io_config.scl_speed_hz = I2C_SCL_FREQ_HZ;
  // Attach the TOUCH to the I2C bus
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(i2c_get_bus_handle(), &tp_io_config, &tp_io_handle));

  const esp_lcd_touch_config_t tp_cfg = {
      .x_max = LV_HOR_RES,
      .y_max = LV_VER_RES,
      .rst_gpio_num = TP_RST,
  #ifdef TP_INT
      .int_gpio_num = TP_INT,
  #endif
      .flags =
          {
              .swap_xy = 0,
              .mirror_x = 0,
              .mirror_y = 0,
          },
  };

  #if TP_CST816S
  ESP_LOGI(TAG, "Initialize touch controller CST816S");
  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &touch_handle));
  #elif TP_FT3168
  ESP_LOGI(TAG, "Initialize touch controller FT3168");
  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft3168(tp_io_handle, &tp_cfg, &touch_handle));
  #elif TP_CST9217
  ESP_LOGI(TAG, "Initialize touch controller CST9217");
  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst9217(tp_io_handle, &tp_cfg, &touch_handle));
  #endif

  return ESP_OK;
}

static void lv_touch_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  if (i2c_lock(10)) {
    // Call the original read callback
    if (original_read_cb) {
      original_read_cb(indev, data);
      static bool was_pressed = false;
      bool was_press = (data->state == LV_INDEV_STATE_PRESSED);
      if (was_press && !was_pressed) {
        // Reset the sleep timer when touch is pressed
        reset_sleep_timer();
      }

      was_pressed = was_press;
      i2c_unlock();
    }
    else {
      ESP_LOGE(TAG, "Original read callback is NULL");
      i2c_unlock();
      return;
    }
  }
}

#endif // TOUCH_ENABLED

void display_set_rotation(ScreenRotation rot) {
  lv_display_set_rotation(lvgl_disp, (lv_display_rotation_t)rot);
}

static esp_err_t app_lvgl_init(void) {
  ESP_LOGI(TAG, "Initialize LVGL library");

  /* Initialize LVGL */
  // const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  const lvgl_port_cfg_t lvgl_cfg = {
      .task_priority = LVGL_TASK_PRIORITY,         /* LVGL task priority */
      .task_stack = LVGL_TASK_STACK_SIZE,          /* LVGL task stack size */
      .task_affinity = LVGL_TASK_CPU_AFFINITY,     /* LVGL task pinned to core (-1 is no affinity) */
      .task_max_sleep_ms = LVGL_TASK_MAX_DELAY_MS, /* Maximum sleep in LVGL task */
      .timer_period_ms = LVGL_TICK_PERIOD_MS       /* LVGL timer tick period in ms */
  };

  esp_err_t err = lvgl_port_init(&lvgl_cfg);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "LVGL port initialization failed");
    return err;
  }

  /* Add LCD screen */
  ESP_LOGD(TAG, "Add LCD screen");
  const lvgl_port_display_cfg_t disp_cfg = {.io_handle = lcd_io,
                                            .panel_handle = lcd_panel,
                                            .buffer_size = BUFFER_SIZE,
                                            .double_buffer = true,
                                            .hres = LV_HOR_RES,
                                            .vres = LV_VER_RES,
                                            .monochrome = false,
                                            .color_format = LV_COLOR_FORMAT_RGB565,
                                            .rotation =
                                                {
                                                    .swap_xy = false,
#if DISP_GC9A01
                                                    .mirror_x = true,
                                                    .mirror_y = false,
#elif DISP_SH8601 || DISP_CO5300
                                                    .mirror_x = false,
                                                    .mirror_y = false,
#elif DISP_ST7789
  #error "ST7789 not supported"
#endif
                                                },
                                            .flags = {
                                                .buff_dma = true,
                                                .buff_spiram = false,
                                                .full_refresh = false,
                                                .direct_mode = false,
                                                .swap_bytes = false,
#if SW_ROTATE
  // .sw_rotate = true, // TODO - figure out why this causes mem issues
#endif
                                            }};

  lvgl_disp = lvgl_port_add_disp(&disp_cfg);

#if ROUNDER_CALLBACK
  lv_display_add_event_cb(lvgl_disp, LVGL_port_rounder_callback, LV_EVENT_INVALIDATE_AREA, NULL);
#endif

  // display_set_rotation(device_settings.screen_rotation); // TODO - figure out why this causes mem issues

#if TOUCH_ENABLED
  const lvgl_port_touch_cfg_t touch_cfg = {
      .disp = lvgl_disp,
      .handle = touch_handle,
  };
  lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);

  // Replace the read callback with our own wrapped with mutex control
  original_read_cb = lv_indev_get_read_cb(lvgl_touch_indev);
  lv_indev_set_read_cb(lvgl_touch_indev, lv_touch_cb);
#endif

  // Initialize the encoder driver
  lvgl_encoder_indev = lv_indev_create();
  lv_indev_set_type(lvgl_encoder_indev, LV_INDEV_TYPE_ENCODER);
  lv_indev_set_read_cb(lvgl_encoder_indev, encoder_read_cb);

  return ESP_OK;
}

lv_indev_t *get_encoder() {
  if (lvgl_encoder_indev == NULL) {
    ESP_LOGE(TAG, "Encoder indev is not initialized");
    return NULL;
  }
  return lvgl_encoder_indev;
}

#if TOUCH_ENABLED
lv_indev_t *get_touch() {
  if (lvgl_touch_indev == NULL) {
    ESP_LOGE(TAG, "Touch indev is not initialized");
    return NULL;
  }
  return lvgl_touch_indev;
}
#endif

#if SCREEN_TEST_UI
static void event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_CLICKED) {
    LV_LOG_USER("Clicked");
    ESP_LOGI(TAG, "Clicked");
    LVGL_lock(0);
    ui_init();
    LVGL_unlock();
  }
  else if (code == LV_EVENT_VALUE_CHANGED) {
    LV_LOG_USER("Toggled");
  }
}
#endif

static esp_err_t display_ui() {
  ESP_LOGI(TAG, "Display UI");

  if (LVGL_lock(0)) {
    reload_theme();
#if SCREEN_TEST_UI // Useful for debugging mutexes and any sl generated code
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xFF69B4), LV_PART_MAIN);
    lv_obj_t *btn = lv_btn_create(lv_scr_act());
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, event_handler, LV_EVENT_ALL, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "Hello world");
    lv_obj_center(label);
    // lv_demo_widgets();
#else
    // ui_init(); // Generated SL UI
    // Use generated ui_init() function here without theme apply
    ui____initial_actions0 = lv_obj_create(NULL);
    ui_SplashScreen_screen_init();
    ui_StatsScreen_screen_init();
    ui_MenuScreen_screen_init();
    lv_disp_load_scr(ui_SplashScreen);
    apply_ui_scale(ui_StatsScreen);
    apply_ui_scale(ui_MenuScreen);
#endif
    LVGL_unlock();
    // Delay backlight turn on to avoid flickering
    vTaskDelay(pdMS_TO_TICKS(250));
    display_set_bl_level(device_settings.bl_level);
    return ESP_OK;
  }
  return ESP_FAIL;
}

void display_deinit() {
  ESP_LOGI(TAG, "Deinit display");
  display_set_bl_level(0);

  if (lv_is_initialized()) {

    if (LVGL_lock(0)) {
#if TOUCH_ENABLED
      // Remove touch
      ESP_ERROR_CHECK(lvgl_port_remove_touch(lvgl_touch_indev));
      ESP_ERROR_CHECK(esp_lcd_touch_del(touch_handle));
#endif

      // Remove panel
      ESP_ERROR_CHECK(lvgl_port_remove_disp(lvgl_disp));
      ESP_ERROR_CHECK(esp_lcd_panel_del(lcd_panel));
      ESP_ERROR_CHECK(esp_lcd_panel_io_del(lcd_io));
      ESP_ERROR_CHECK(spi_bus_free(LCD_HOST));

      LVGL_unlock();
    }

    // Stop LVGL
    ESP_ERROR_CHECK(lvgl_port_deinit());
  }
}

void display_init() {
  ESP_LOGI(TAG, "Create LVGL conf");
  /* LCD HW initialization */
  ESP_ERROR_CHECK(app_lcd_init());
#if TOUCH_ENABLED
  /* Touch initialization */
  ESP_ERROR_CHECK(app_touch_init());
#endif
  /* LVGL initialization */
  ESP_ERROR_CHECK(app_lvgl_init());
  is_initialized = true;
  ESP_ERROR_CHECK(display_ui());
}

void display_off() {
  ESP_LOGI(TAG, "Display sleep");
  // Turn off display
  if (lcd_panel) {
    esp_lcd_panel_disp_on_off(lcd_panel, false);
    esp_lcd_panel_disp_sleep(lcd_panel, true);
  }

#if TOUCH_ENABLED
  if (touch_handle) {
    esp_lcd_touch_enter_sleep(touch_handle);
  }
#endif
}