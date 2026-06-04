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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/ledc_types.h"
#include "powermanagement.h"
#include "remote/i2c.h"
#include "settings.h"
#include "remoteinputs.h"

// Slint inclusion
#include "slint-esp.h"
#include "generated/app-window.h"
#include "esp_heap_caps.h"

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
  #define ROUNDER_CALLBACK 1
  #include "display/sh8601/display_driver_sh8601.h"
  #include "esp_lcd_sh8601.h"
  #define RGB_ELE_ORDER LCD_RGB_ELEMENT_ORDER_RGB
#elif DISP_ST7789
  #error "ST7789 not supported"
#endif

#include "remote/haptic.h"

static const char *TAG = "PUBREMOTE-DISPLAY";

#define LCD_HOST SPI2_HOST
#define LCD_CMD_BITS 8
#define LCD_PARAM_BITS 8
#define MAX_TRAN_SIZE (LV_HOR_RES * LV_VER_RES * sizeof(uint16_t))

static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;
static slint::platform::Rgb565Pixel *frame_buffer = nullptr;

static bool is_initialized = false;
static uint8_t bl_level = 0;

static SlintWindowPtr slint_window;
static TaskHandle_t slint_task_handle = NULL;
static TaskHandle_t slint_input_task_handle = NULL;

SlintWindowPtr get_slint_window() {
  return slint_window;
}

#include <atomic>

static std::atomic<Screen> cached_active_screen(Screen::Splash);

extern "C" bool is_stats_screen_active() {
  return cached_active_screen.load() == Screen::Stats;
}

extern "C" bool is_pairing_screen_active() {
  return cached_active_screen.load() == Screen::Pairing;
}

extern "C" bool is_about_screen_active() {
  return cached_active_screen.load() == Screen::About;
}

extern "C" bool is_calibration_screen_active() {
  return cached_active_screen.load() == Screen::Calibration;
}

extern "C" bool is_menu_screen_active() {
  return cached_active_screen.load() == Screen::Menu;
}

extern "C" bool is_settings_screen_active() {
  return cached_active_screen.load() == Screen::Settings;
}

extern "C" bool is_charge_screen_active() {
  return cached_active_screen.load() == Screen::Charge;
}

extern "C" bool is_update_screen_active() {
  return cached_active_screen.load() == Screen::Update;
}


extern "C" uint8_t display_get_bl_level() {
  return bl_level;
}

extern "C" void display_set_bl_level(uint8_t level) {
  ESP_LOGI(TAG, "display_set_bl_level: %d (is_initialized: %d)", level, is_initialized);
  if (is_initialized) {
    bl_level = level;
    set_display_brightness(lcd_io, bl_level);
  }
}

// Declarations of screen handlers implemented in other files
extern "C" {
void handle_splash_tapped();
void handle_stats_swiped_down();
void handle_menu_back();
void handle_menu_connect();
void handle_menu_pocket_mode();
void handle_open_settings();
void handle_open_calibration();
void handle_open_pairing();
void handle_open_about();
void handle_menu_shutdown();
void handle_settings_save();
void handle_pairing_action();
void handle_calibration_primary();
void handle_calibration_secondary();
void handle_about_check_updates();
void handle_about_back();
void handle_update_primary();
void handle_update_secondary();
void handle_update_selected(int index);
}

static void connect_callbacks() {
  const auto &state = slint_window->global<UiState>();

  state.on_screen_changed([](Screen screen) {
    cached_active_screen.store(screen);
  });

  state.on_splash_tapped([]() { handle_splash_tapped(); });
  state.on_stats_swiped_down([]() { handle_stats_swiped_down(); });
  state.on_menu_back([]() { handle_menu_back(); });
  state.on_menu_connect([]() { handle_menu_connect(); });
  state.on_menu_pocket_mode([]() { handle_menu_pocket_mode(); });
  state.on_open_settings([]() { handle_open_settings(); });
  state.on_open_calibration([]() { handle_open_calibration(); });
  state.on_open_pairing([]() { handle_open_pairing(); });
  state.on_open_about([]() { handle_open_about(); });
  state.on_menu_shutdown([]() { handle_menu_shutdown(); });
  state.on_settings_save([]() { handle_settings_save(); });
  state.on_pairing_action([]() { handle_pairing_action(); });
  state.on_calibration_primary([]() { handle_calibration_primary(); });
  state.on_calibration_secondary([]() { handle_calibration_secondary(); });
  state.on_about_check_updates([]() { handle_about_check_updates(); });
  state.on_about_back([]() { handle_about_back(); });
  state.on_update_primary([]() { handle_update_primary(); });
  state.on_update_secondary([]() { handle_update_secondary(); });
  state.on_update_selected([](int index) { handle_update_selected(index); });
}

// Hardware settings apply
extern "C" void apply_theme_settings() {
  const auto &theme = slint_window->global<Theme>();
  
  // Set the actual panel resolution dynamically based on the current screen size
  theme.set_panel_res(LV_HOR_RES);

  // Custom accent color
  theme.set_accent(slint::Color::from_argb_encoded(device_settings.theme_color));

  // Dark text mode (with dynamic background)
  if (device_settings.dark_text) {
    theme.set_bg(slint::Color::from_rgb_uint8(255, 255, 255));
    theme.set_text(slint::Color::from_rgb_uint8(0, 0, 0));
    theme.set_text_dim(slint::Color::from_rgb_uint8(100, 100, 100));
  } else {
    theme.set_bg(slint::Color::from_rgb_uint8(0, 0, 0));
    theme.set_text(slint::Color::from_rgb_uint8(255, 255, 255));
    theme.set_text_dim(slint::Color::from_rgb_uint8(154, 154, 154));
  }
}

// Event loop thread
static void slint_event_loop(void *pvParameters) {
  ESP_LOGI(TAG, "Slint task started");
  
  // Initialize Slint platform with our configuration
  SlintPlatformConfiguration<slint::platform::Rgb565Pixel> config;
  config.size = slint::PhysicalSize(slint::Size<uint32_t>{(uint32_t)LV_HOR_RES, (uint32_t)LV_VER_RES});
  config.panel_handle = lcd_panel;
  config.touch_handle = touch_handle;
  config.byte_swap = true; // Swap bytes for standard SPI/QSPI big-endian display interfaces
  if (frame_buffer) {
    config.buffer1 = std::span<slint::platform::Rgb565Pixel>(frame_buffer, LV_HOR_RES * LV_VER_RES);
  }
  
  // Map rotation
  switch (device_settings.screen_rotation) {
    case SCREEN_ROTATION_90:
      config.rotation = slint::platform::SoftwareRenderer::RenderingRotation::Rotate90;
      break;
    case SCREEN_ROTATION_180:
      config.rotation = slint::platform::SoftwareRenderer::RenderingRotation::Rotate180;
      break;
    case SCREEN_ROTATION_270:
      config.rotation = slint::platform::SoftwareRenderer::RenderingRotation::Rotate270;
      break;
    default:
      config.rotation = slint::platform::SoftwareRenderer::RenderingRotation::NoRotation;
      break;
  }
  
  ESP_LOGI(TAG, "Initializing Slint ESP platform...");
  slint_esp_init(config);
  
  ESP_LOGI(TAG, "Creating AppWindow...");
  slint_window = AppWindow::create();
  connect_callbacks();
  apply_theme_settings();
  
  ESP_LOGI(TAG, "Applying initial backlight level: 0");
  display_set_bl_level(0);
  vTaskDelay(pdMS_TO_TICKS(350));
  ESP_LOGI(TAG, "Restoring target backlight level: %d", device_settings.bl_level);
  display_set_bl_level(device_settings.bl_level);
  
  // Blocks until event loop ends
  ESP_LOGI(TAG, "Running Slint window event loop...");
  slint_window->run();
  
  ESP_LOGI(TAG, "Slint event loop exited");
  vTaskDelete(NULL);
}

// Input polling task to map remote control buttons/joystick to Slint navigation
static void slint_input_task(void *pvParameters) {
  static bool was_pressed = false;
  static int last_dir = 0; // -1: up, 1: down, 0: center
  static uint32_t last_move_time = 0;

  while (true) {
    if (slint_window) {
      // 1. Button C mapping to Return/Enter key
      bool is_pressed = remote_data.bt_c;
      if (is_pressed != was_pressed) {
        was_pressed = is_pressed;
        slint::invoke_from_event_loop([=]() {
          if (is_pressed) {
            slint_window->window().dispatch_key_press_event("\n");
          } else {
            slint_window->window().dispatch_key_release_event("\n");
          }
        });
      }

      // 2. Joystick Y mapping to Tab/Backtab for focus navigation
      int current_dir = 0;
      if (remote_data.js_y > 0.5) {
        current_dir = 1; // Down -> Tab
      } else if (remote_data.js_y < -0.5) {
        current_dir = -1; // Up -> Backtab
      }

      uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if (current_dir != 0) {
        if (current_dir != last_dir || (now - last_move_time) > 250) {
          last_dir = current_dir;
          last_move_time = now;
          slint::invoke_from_event_loop([=]() {
            if (current_dir == 1) {
              slint_window->window().dispatch_key_press_event("\t");
              slint_window->window().dispatch_key_release_event("\t");
            } else {
              slint_window->window().dispatch_key_press_event("\x19"); // Backtab
              slint_window->window().dispatch_key_release_event("\x19");
            }
          });
        }
      } else {
        last_dir = 0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

extern "C" void display_set_rotation(ScreenRotation rot) {
  if (is_initialized) {
    slint::platform::SoftwareRenderer::RenderingRotation slint_rot;
    switch (rot) {
      case SCREEN_ROTATION_90:
        slint_rot = slint::platform::SoftwareRenderer::RenderingRotation::Rotate90;
        break;
      case SCREEN_ROTATION_180:
        slint_rot = slint::platform::SoftwareRenderer::RenderingRotation::Rotate180;
        break;
      case SCREEN_ROTATION_270:
        slint_rot = slint::platform::SoftwareRenderer::RenderingRotation::Rotate270;
        break;
      default:
        slint_rot = slint::platform::SoftwareRenderer::RenderingRotation::NoRotation;
        break;
    }
    slint_esp_set_rotation(slint_rot);
  }
}

static SemaphoreHandle_t trans_sem = NULL;
#define CHUNK_LINES 20
static uint16_t *chunk_buffer = NULL;

static bool IRAM_ATTR on_lcd_color_trans_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
  BaseType_t high_task_awoken = pdFALSE;
  xSemaphoreGiveFromISR(trans_sem, &high_task_awoken);
  return high_task_awoken == pdTRUE;
}

extern "C" void display_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const uint16_t *color_data) {
  if (!is_initialized || !chunk_buffer || !trans_sem) {
    return;
  }
  int width = x_end - x_start;
  int height = y_end - y_start;
  int row_stride = LV_HOR_RES;

  for (int y = y_start; y < y_end; y += CHUNK_LINES) {
    int current_chunk_lines = std::min(CHUNK_LINES, y_end - y);
    
    // Copy chunk from PSRAM to Internal SRAM DMA buffer
    for (int line = 0; line < current_chunk_lines; ++line) {
      const uint16_t *src = color_data + (y + line) * row_stride + x_start;
      uint16_t *dst = chunk_buffer + line * width;
      memcpy(dst, src, width * sizeof(uint16_t));
    }
    
    // Draw chunk
    esp_lcd_panel_draw_bitmap(lcd_panel, x_start, y, x_end, y + current_chunk_lines, chunk_buffer);
    
    // Wait for transfer complete
    xSemaphoreTake(trans_sem, portMAX_DELAY);
  }
}

static esp_err_t app_lcd_init(void) {
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

#if DISP_GC9A01
  esp_lcd_panel_io_spi_config_t io_config = GC9A01_PANEL_IO_SPI_CONFIG(DISP_CS, DISP_DC, on_lcd_color_trans_done, NULL);
  io_config.trans_queue_depth = 20;
#elif DISP_SH8601 || DISP_CO5300
  esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(DISP_CS, on_lcd_color_trans_done, NULL);
  io_config.pclk_hz = LCD_PIXEL_CLOCK_HZ;
  io_config.trans_queue_depth = 20;
#endif

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

  const esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = DISP_RST,
      .rgb_ele_order = RGB_ELE_ORDER,
      .bits_per_pixel = 16,
      .vendor_config = &vendor_config,
  };

#if DISP_GC9A01
  ESP_LOGI(TAG, "Install GC9A01 panel driver");
  esp_err_t init_err = esp_lcd_new_panel_gc9a01(lcd_io, &panel_config, &lcd_panel);
#elif DISP_SH8601 || DISP_CO5300
  ESP_LOGI(TAG, "Install SH8601/CO5300 panel driver");
  esp_err_t init_err = esp_lcd_new_panel_sh8601(lcd_io, &panel_config, &lcd_panel);
#endif

  if (init_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install LCD driver");
    if (lcd_panel) esp_lcd_panel_del(lcd_panel);
    if (lcd_io) esp_lcd_panel_io_del(lcd_io);
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
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(lcd_panel, false, false));
  esp_lcd_panel_disp_on_off(lcd_panel, true);
  ESP_ERROR_CHECK(test_display_communication(lcd_io));

  return ret;
}

#if TOUCH_ENABLED
static esp_err_t app_touch_init(void) {
  esp_lcd_panel_io_handle_t tp_io_handle = NULL;

  #if TP_CST816S
  esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
  #elif TP_FT3168
  esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
  #elif TP_CST9217
  esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
  #endif

  tp_io_config.scl_speed_hz = I2C_SCL_FREQ_HZ;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(i2c_get_bus_handle(), &tp_io_config, &tp_io_handle));

  const esp_lcd_touch_config_t tp_cfg = {
      .x_max = LV_HOR_RES,
      .y_max = LV_VER_RES,
      .rst_gpio_num = (gpio_num_t)TP_RST,
  #ifdef TP_INT
      .int_gpio_num = (gpio_num_t)TP_INT,
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
  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &touch_handle));
  #elif TP_CST9217
  ESP_LOGI(TAG, "Initialize touch controller CST9217");
  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst9217(tp_io_handle, &tp_cfg, &touch_handle));
  #endif

  return ESP_OK;
}
#endif

extern "C" void display_init() {
  ESP_LOGI(TAG, "Initializing Slint display wrapper");
  
  trans_sem = xSemaphoreCreateBinary();
  chunk_buffer = (uint16_t *)heap_caps_malloc(LV_HOR_RES * CHUNK_LINES * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!chunk_buffer) {
    ESP_LOGE(TAG, "Failed to allocate chunk buffer!");
    abort();
  }
  
  ESP_ERROR_CHECK(app_lcd_init());
#if TOUCH_ENABLED
  ESP_ERROR_CHECK(app_touch_init());
#endif

  size_t fb_size = LV_HOR_RES * LV_VER_RES * sizeof(slint::platform::Rgb565Pixel);
  // Try allocating in Internal RAM first to avoid CACHE-126 PSRAM cache bug on ESP32-S3 if it fits
  frame_buffer = (slint::platform::Rgb565Pixel *)heap_caps_malloc(fb_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!frame_buffer) {
    ESP_LOGI(TAG, "Failed to allocate frame buffer in Internal RAM, falling back to PSRAM...");
    frame_buffer = (slint::platform::Rgb565Pixel *)heap_caps_malloc(fb_size, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
  }

  if (!frame_buffer) {
    ESP_LOGE(TAG, "Failed to allocate frame buffer!");
    abort();
  } else {
    ESP_LOGI(TAG, "Frame buffer allocated at %p (%zu bytes)", frame_buffer, fb_size);
    memset(frame_buffer, 0, fb_size);
  }

  is_initialized = true;

  // Start Slint Event Loop Task
  xTaskCreatePinnedToCore(slint_event_loop, "slint_event_loop", 64 * 1024, NULL, 20, &slint_task_handle, 1);
  
  // Start Input Polling Task
  xTaskCreate(slint_input_task, "slint_input_task", 4096, NULL, 20, &slint_input_task_handle);
}

extern "C" void display_deinit() {
  ESP_LOGI(TAG, "Deinit display");
  display_set_bl_level(0);
  if (slint_window) {
    slint::quit_event_loop();
    slint_window.reset();
  }
  vTaskDelay(pdMS_TO_TICKS(100));
  
  if (touch_handle) {
    esp_lcd_touch_del(touch_handle);
    touch_handle = NULL;
  }
  if (lcd_panel) {
    esp_lcd_panel_del(lcd_panel);
    lcd_panel = NULL;
  }
  if (lcd_io) {
    esp_lcd_panel_io_del(lcd_io);
    lcd_io = NULL;
  }
  spi_bus_free(LCD_HOST);

  if (frame_buffer) {
    free(frame_buffer);
    frame_buffer = nullptr;
  }

  is_initialized = false;
}

extern "C" void display_off() {
  ESP_LOGI(TAG, "Display sleep");
  if (lcd_panel) {
    esp_lcd_panel_disp_on_off(lcd_panel, false);
    esp_lcd_panel_disp_sleep(lcd_panel, true);
  }
  if (touch_handle) {
    esp_lcd_touch_enter_sleep(touch_handle);
  }
}
