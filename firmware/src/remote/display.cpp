#include "display.h"
#include "config.h"
#include "display/display_driver.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "generated/app-window.h"
#include "hal/ledc_types.h"
#include "powermanagement.h"
#include "remote/color_utils.h"
#include "remote/i2c.h"
#include "remote/imu.h"
#include "remote/led.h"
#include "remoteinputs.h"
#include "screens/about_screen.h"
#include "screens/boards_screen.h"
#include "screens/imu_calibration_screen.h"
#include "screens/input_calibration_screen.h"
#include "screens/menu_screen.h"
#include "screens/pairing_screen.h"
#include "screens/settings_screen.h"
#include "screens/stats_screen.h"
#include "screens/update_screen.h"
#include "settings.h"
#include "slint-esp.h"
#include "utilities/mem_debug.h"

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

#include "remote/haptic.h"

static const char *TAG = "PUBREMOTE-DISPLAY";

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

uint16_t *slint_chunk_buffer[SLINT_CHUNK_ACCUMULATORS] = {};
// Three staging buffers instead of two, so keep each shorter and hold the total roughly where
// the double-buffered pair was - this is internal DMA-capable RAM and there is little spare.
int slint_chunk_lines = VER_RES / 30;

// PSRAM frame buffers removed for pure chunked mode

static SlintWindowPtr slint_window;
static TaskHandle_t slint_task_handle = NULL;
static TaskHandle_t slint_input_task_handle = NULL;

AppWindow *get_slint_window() {
  return slint_window ? &*slint_window : nullptr;
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

extern "C" bool is_imu_calibration_screen_active() {
  return cached_active_screen.load() == Screen::ImuCalibration;
}

extern "C" bool is_input_calibration_screen_active() {
  return cached_active_screen.load() == Screen::InputCalibration;
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

extern "C" const char *hbm_mode_label(HbmModeOptions mode) {
  switch (mode) {
  case HBM_MODE_OFF:
    return "OFF";
  case HBM_MODE_ON:
    return "ON";
  case HBM_MODE_RAISED:
    return "RAISED";
  default:
    return "OFF";
  }
}

extern "C" void display_set_joystick_supported(bool supported) {
  if (!get_slint_window()) {
    // UI not up yet - connect_callbacks() picks the values up on init
    return;
  }

  const bool x_supported = input_pins_x_enabled();
  const bool y_supported = input_pins_y_enabled();

  slint::invoke_from_event_loop([supported, x_supported, y_supported]() {
    const auto &state = get_slint_window()->global<UiState>();
    state.set_joystick_supported(supported);
    state.set_joystick_x_supported(x_supported);
    state.set_joystick_y_supported(y_supported);
  });
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

// Declarations of screen handlers implemented in other files
extern "C"
{
  void handle_splash_tapped();
  void handle_stats_swiped_down();
  void handle_menu_back();
  void handle_menu_connect();
  void handle_menu_pocket_mode();
  void handle_menu_toggle_hbm();
  void handle_menu_toggle_led();
  void handle_open_settings();
  void handle_open_input_calibration();
  void handle_open_pairing();
  void handle_open_about();
  void handle_open_imu_calibration();
  void handle_menu_shutdown();
  void handle_settings_save();
  void handle_settings_changed();
  void handle_pairing_action();
  void setup_boards_properties();
  void teardown_boards_properties();
  void setup_pairing_properties();
  void teardown_pairing_properties();
  void setup_menu_properties();
  void teardown_menu_properties();
  void handle_select_board(int index);
  void handle_delete_board(int index);
  void handle_boards_back();
  void handle_boards_pair_new();
  void handle_input_calibration_primary();
  void handle_input_calibration_secondary();
  void handle_about_check_updates();
  void handle_about_back();
  void handle_imu_calibration_back();
  void handle_imu_calibration_primary();
  void handle_update_primary();
  void handle_update_secondary();
  void handle_update_selected(int index);
}

#include <algorithm>
#include <cmath>

extern "C" void handle_imu_gesture(imu_gesture_t gesture);

extern "C"
{
  extern volatile uint32_t slint_esp_prepare_us;
  extern volatile uint32_t slint_esp_render_us;
  extern volatile uint32_t slint_esp_flush_us;
  extern volatile uint32_t slint_esp_dirty_px;
}

static void connect_callbacks() {
  const auto &state = slint_window->global<UiState>();

  state.set_show_fps(SHOW_FPS);
  state.set_imu_supported(IMU_ENABLED);
  state.set_joystick_supported(input_pins_joystick_enabled());
  state.set_joystick_x_supported(input_pins_x_enabled());
  state.set_joystick_y_supported(input_pins_y_enabled());

  state.on_screen_changed([](Screen screen) {
    Screen prev = cached_active_screen.exchange(screen);
    if (prev != screen) {
      // Exit hooks
      if (prev == Screen::Stats) {
        teardown_stats_properties();
      }
      else if (prev == Screen::Pairing) {
        teardown_pairing_properties();
      }
      else if (prev == Screen::Boards) {
        teardown_boards_properties();
      }
      else if (prev == Screen::Menu) {
        teardown_menu_properties();
      }

      // Enter hooks
      if (screen == Screen::Stats) {
        setup_stats_properties();
      }
      else if (screen == Screen::Menu) {
        setup_menu_properties();
      }
      else if (screen == Screen::Settings) {
        setup_settings_properties();
      }
      else if (screen == Screen::InputCalibration) {
        setup_input_calibration_properties();
      }
      else if (screen == Screen::Pairing) {
        setup_pairing_properties();
      }
      else if (screen == Screen::About) {
        setup_about_properties();
      }
      else if (screen == Screen::ImuCalibration) {
        setup_imu_calibration_properties();
      }
      else if (screen == Screen::Update) {
        setup_update_properties();
      }
      else if (screen == Screen::Boards) {
        setup_boards_properties();
      }
    }
  });

  state.on_splash_tapped([]() { handle_splash_tapped(); });
  state.on_stats_swiped_down([]() { handle_stats_swiped_down(); });
  state.on_menu_back([]() { handle_menu_back(); });
  state.on_menu_connect([]() { handle_menu_connect(); });
  state.on_menu_pocket_mode([]() { handle_menu_pocket_mode(); });
  state.on_menu_toggle_hbm([]() { handle_menu_toggle_hbm(); });
  state.on_menu_toggle_led([]() { handle_menu_toggle_led(); });
  state.on_open_settings([]() { handle_open_settings(); });
  state.on_open_input_calibration([]() { handle_open_input_calibration(); });
  state.on_open_pairing([]() { handle_open_pairing(); });
  state.on_open_about([]() { handle_open_about(); });
  state.on_open_imu_calibration([]() { handle_open_imu_calibration(); });
  state.on_imu_calibration_back([]() { handle_imu_calibration_back(); });
  state.on_imu_calibration_primary([]() { handle_imu_calibration_primary(); });
  state.on_menu_shutdown([]() { handle_menu_shutdown(); });
  state.on_settings_save([]() { handle_settings_save(); });
  state.on_settings_changed([]() { handle_settings_changed(); });
  state.on_pairing_action([]() { handle_pairing_action(); });
  state.on_select_board([](int index) { handle_select_board(index); });
  state.on_delete_board([](int index) { handle_delete_board(index); });
  state.on_boards_back([]() { handle_boards_back(); });
  state.on_boards_pair_new([]() { handle_boards_pair_new(); });
  state.on_input_calibration_primary([]() { handle_input_calibration_primary(); });
  state.on_input_calibration_secondary([]() { handle_input_calibration_secondary(); });
  state.on_about_check_updates([]() { handle_about_check_updates(); });
  state.on_about_back([]() { handle_about_back(); });
  state.on_update_primary([]() { handle_update_primary(); });
  state.on_update_secondary([]() { handle_update_secondary(); });
  state.on_update_selected([](int index) { handle_update_selected(index); });

  const auto &color_slider_gen = slint_window->global<ColorSliderGenerator>();
  color_slider_gen.on_generate_track(generate_color_slider_track);
}

// Hardware settings apply
extern "C" void apply_theme_settings() {
  const auto &theme = slint_window->global<Theme>();

  // Set the actual panel resolution dynamically based on the current screen size
  theme.set_panel_res(std::min(HOR_RES, VER_RES));
  theme.set_panel_width(HOR_RES);
  theme.set_panel_height(VER_RES);

  // Set the screen shape mode (false = circular, true = square/rectangular)
  theme.set_is_square_mode(UI_SHAPE == 1);

  uint8_t r = (device_settings.theme_color >> 16) & 0xFF;
  uint8_t g = (device_settings.theme_color >> 8) & 0xFF;
  uint8_t b = device_settings.theme_color & 0xFF;

  // Custom accent color
  theme.set_accent(slint::Color::from_rgb_uint8(r, g, b));
  float luminance = 0.299f * r + 0.587f * g + 0.114f * b;

  if (luminance > 140.0f) {
    theme.set_primary_button_text(slint::Color::from_rgb_uint8(0, 0, 0));
  }
  else {
    theme.set_primary_button_text(slint::Color::from_rgb_uint8(255, 255, 255));
  }

  theme.set_bg(slint::Color::from_rgb_uint8(0, 0, 0));
  theme.set_text(slint::Color::from_rgb_uint8(255, 255, 255));
  theme.set_text_dim(slint::Color::from_rgb_uint8(154, 154, 154));
}

// Event loop thread
static void slint_event_loop(void *pvParameters) {
  ESP_LOGI(TAG, "Slint task started");

  // Initialize Slint platform with our configuration
  SlintPlatformConfiguration<slint::platform::Rgb565BigEndianPixel> config;
  config.size = slint::PhysicalSize(slint::Size<uint32_t>{(uint32_t)HOR_RES, (uint32_t)VER_RES});
  config.panel_handle = lcd_panel;
  config.touch_handle = touch_handle;
  config.byte_swap = false;

  // config.buffer1/buffer2 are deliberately left unset: that selects render_by_line
  // chunked mode, which stages into slint_chunk_buffer in internal SRAM. Full-frame
  // buffers would have to live in PSRAM, and reading them back per frame is slower than
  // rendering into SRAM chunks on this panel.
  ESP_LOGI(TAG, "Using internal SRAM chunked rendering mode");

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
  MEM_MARK("pre AppWindow");
  slint_window = AppWindow::create();
  MEM_MARK("post AppWindow");

  // Surface unexpected reboots (panic / watchdog / brownout) as a dismissable
  // dialog so crashes don't go unnoticed as a silent restart
  switch (esp_reset_reason()) {
  case ESP_RST_PANIC:
    slint_window->global<UiState>().set_boot_notice("The remote restarted after a firmware crash");
    break;
  case ESP_RST_TASK_WDT:
    slint_window->global<UiState>().set_boot_notice("The remote restarted because a task stopped responding");
    break;
  case ESP_RST_INT_WDT:
  case ESP_RST_WDT:
    slint_window->global<UiState>().set_boot_notice("The remote restarted after a watchdog timeout");
    break;
  case ESP_RST_BROWNOUT:
    slint_window->global<UiState>().set_boot_notice("The remote restarted due to a power brownout");
    break;
  default:
    break;
  }
  connect_callbacks();
  apply_theme_settings();
  MEM_MARK("post callbacks");

  ESP_LOGI(TAG, "Applying initial backlight level: 0");
  display_set_bl_level(0);
  vTaskDelay(pdMS_TO_TICKS(350));
  ESP_LOGI(TAG, "Restoring target backlight level: %d", device_settings.bl_level);
  display_set_bl_level(device_settings.bl_level);
  if (device_settings.hbm_mode == HBM_MODE_ON) {
    display_set_hbm(true);
  }
  else {
    display_set_hbm(false);
  }

  // Subscribe this task to the task watchdog and feed it from a Slint timer:
  // timers are dispatched by the event loop itself, so a wedged event loop
  // (frozen UI) stops the feed and the watchdog panics + reboots
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
  slint::Timer wdt_feed_timer;
  wdt_feed_timer.start(slint::TimerMode::Repeated, std::chrono::milliseconds(500), []() { esp_task_wdt_reset(); });

  // Blocks until event loop ends
  MEM_MARK("pre run loop");
  ESP_LOGI(TAG, "Running Slint window event loop...");
  slint_window->run();

  ESP_LOGI(TAG, "Slint event loop exited");
  wdt_feed_timer.stop();
  // Released here rather than by whoever asked us to quit: AppWindow owns slint::Timers and
  // those may only be destroyed on this thread.
  slint_window.reset();
  esp_task_wdt_delete(NULL);
  slint_task_handle = NULL;
  vTaskDelete(NULL);
}

// Input polling task to map remote control buttons/joystick to Slint navigation
static void slint_input_task(void *pvParameters) {
  static bool was_pressed = false;
  static int last_dir = 0; // -1: up, 1: down, 0: center

#if SHOW_FPS
  static uint32_t last_fps_time = 0;
  static uint32_t last_frame_count = 0;
#endif

  while (true) {
    if (slint_window) {
      // 1. Button C mapping to Return/Enter key
      bool is_pressed = remote_data.bt_c;
      if (is_pressed != was_pressed) {
        was_pressed = is_pressed;
        slint::invoke_from_event_loop([=]() {
          if (is_pressed) {
            slint_window->window().dispatch_key_press_event("\n");
          }
          else {
            slint_window->window().dispatch_key_release_event("\n");
          }
        });
      }

      // 2. Joystick Y mapping to Tab/Backtab for focus navigation
      // Using an edge-triggered approach to prevent rapid scrolling on hold or drift.
      int current_dir = 0;
      if (remote_data.js_y > 0.7) {
        current_dir = 1; // Down -> Tab
      }
      else if (remote_data.js_y < -0.7) {
        current_dir = -1; // Up -> Backtab
      }

      if (current_dir != last_dir) {
        // Trigger only on transition (neutral -> active)
        if (current_dir != 0) {
          slint::invoke_from_event_loop([=]() {
            if (current_dir == 1) {
              slint_window->window().dispatch_key_press_event("\t");
              slint_window->window().dispatch_key_release_event("\t");
            }
            else {
              slint_window->window().dispatch_key_press_event("\x19"); // Backtab
              slint_window->window().dispatch_key_release_event("\x19");
            }
          });
        }
        last_dir = current_dir;
      }

#if SHOW_FPS
      // Frames actually drawn per second, straight from the renderer. The
      // previous version posted closures into the event loop and counted how
      // fast they came back - that measured event-loop dispatch rate, not
      // frame rate, and the closure traffic plus a 1 ms poll perturbed the
      // very thing it was measuring.
      uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if (last_fps_time == 0) {
        // Start the window here rather than at tick 0: otherwise the first sample
        // reports every frame drawn since boot as if it happened in one second.
        last_fps_time = now;
        last_frame_count = slint_esp_frame_counter;
      }
      else if (now - last_fps_time >= 1000) {
        uint32_t current_count = slint_esp_frame_counter;
        uint32_t elapsed = now - last_fps_time;
        // Scale by the real window: this task polls every 30ms, so the window
        // overshoots 1000ms and a raw frame count would read low.
        int fps = (int)(((current_count - last_frame_count) * 1000 + elapsed / 2) / elapsed);
        last_frame_count = current_count;
        last_fps_time = now;
        // Sampled alongside the frame rate so the overlay can show where a frame goes:
        // prepare is scene building, render the per-line rasterisation, flush the byte
        // swap plus waiting on the panel. Reading these off the panel avoids needing a
        // serial monitor, which holds firmware.elf open for the exception decoder.
        int prepare_ms = (int)((slint_esp_prepare_us + 500) / 1000);
        int render_ms = (int)((slint_esp_render_us + 500) / 1000);
        int flush_ms = (int)((slint_esp_flush_us + 500) / 1000);
        int dirty_pct = (int)((slint_esp_dirty_px * 100ULL + (HOR_RES * VER_RES) / 2) / (HOR_RES * VER_RES));
        slint::invoke_from_event_loop([=]() {
          if (slint_window) {
            const auto &st = slint_window->global<UiState>();
            st.set_perf_prepare_ms(prepare_ms);
            st.set_perf_render_ms(render_ms);
            st.set_perf_flush_ms(flush_ms);
            st.set_perf_dirty_pct(dirty_pct);
            slint_window->global<UiState>().set_fps(fps);
          }
        });
      }
#endif
    }
    // One rate, always. Nothing needs sub-30 ms polling now that the frame
    // count comes from the renderer rather than being inferred from how fast
    // this task can round-trip a closure.
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

SemaphoreHandle_t trans_sem = NULL;

static bool IRAM_ATTR on_lcd_color_trans_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata,
                                              void *user_ctx) {
  BaseType_t high_task_awoken = pdFALSE;
  xSemaphoreGiveFromISR(trans_sem, &high_task_awoken);
  return high_task_awoken == pdTRUE;
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#if DISP_GC9A01
  esp_lcd_panel_io_spi_config_t io_config = GC9A01_PANEL_IO_SPI_CONFIG(DISP_CS, DISP_DC, on_lcd_color_trans_done, NULL);
  io_config.trans_queue_depth = 20;
#elif DISP_SH8601 || DISP_CO5300
  esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(DISP_CS, on_lcd_color_trans_done, NULL);
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
      .reset_gpio_num = DISP_RST,
      .rgb_ele_order = RGB_ELE_ORDER,
      .bits_per_pixel = 16,
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
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(i2c_get_bus_handle(), &tp_io_config, &tp_io_handle));

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

extern "C" void display_init() {
  ESP_LOGI(TAG, "Initializing Slint display wrapper");

  // Chunk height must be even. LVGL's rounder_cb aligned BOTH axes to even bounds and had
  // no artifacts; our chunked path only aligns x, so an odd height puts every other chunk
  // on an odd panel row (0, 23, 46, 69...) and shows as horizontal seams at the boundaries.
  if (slint_chunk_lines % 2 != 0) {
    ESP_LOGW(TAG, "slint_chunk_lines %d is odd; using %d so chunks start on even rows", slint_chunk_lines,
             slint_chunk_lines - 1);
    slint_chunk_lines--;
  }
  ESP_LOGI(TAG, "Chunk buffers: %d x %d lines (%u bytes each)", SLINT_CHUNK_ACCUMULATORS, slint_chunk_lines,
           (unsigned)(HOR_RES * slint_chunk_lines * sizeof(uint16_t)));

  // Allocate chunk buffers early to avoid memory fragmentation from the Slint task stack
  for (int i = 0; i < SLINT_CHUNK_ACCUMULATORS; i++) {
    slint_chunk_buffer[i] = (uint16_t *)heap_caps_malloc(HOR_RES * slint_chunk_lines * sizeof(uint16_t),
                                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!slint_chunk_buffer[i]) {
      ESP_LOGE(TAG, "Failed to allocate chunk buffer %d!", i);
      abort();
    }
  }

  // PSRAM frame buffers removed for pure chunked mode

  // Counting, not binary: the flush keeps several chunk transfers outstanding so rendering
  // overlaps the panel DMA, and a binary semaphore would collapse two completions into one.
  trans_sem = xSemaphoreCreateCounting(SLINT_CHUNK_ACCUMULATORS, 0);
  if (!panel_io_lock) {
    panel_io_lock = xSemaphoreCreateRecursiveMutex();
  }
  ESP_ERROR_CHECK(app_lcd_init());
#if TOUCH_ENABLED
  ESP_ERROR_CHECK(app_touch_init());
#endif

  is_initialized = true;

  // Start Slint Event Loop Task in internal SRAM
  // Must be in internal SRAM because NVS flash writes disable CPU caches, causing cache panics if stack is in PSRAM.
  xTaskCreatePinnedToCore(slint_event_loop, "slint_event_loop", 16 * 1024, NULL, 20, &slint_task_handle, 1);

  // NOTE: slint_window is created inside slint_event_loop above, so it is not safe to
  // touch UiState here - see connect_callbacks() for properties set once it exists.

  // Start Input Polling Task (pinned to core 0, leaving core 1 fully dedicated to the Slint event loop)
  xTaskCreatePinnedToCore(slint_input_task, "slint_input_task", 2048, NULL, 20, &slint_input_task_handle, 0);
}

extern "C" void display_deinit() {
  ESP_LOGI(TAG, "Deinit display");
  imu_unregister_gesture_callback(handle_imu_gesture);

  display_set_bl_level(0);
  if (slint_window) {
    // Resetting from this task would destroy AppWindow's timers off
    //  the Slint thread and panic the timer registry.
    slint::quit_event_loop();
    for (int i = 0; i < 100 && slint_window; i++) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (slint_window) {
      ESP_LOGW(TAG, "Slint event loop did not exit; leaving the window allocated");
    }
  }
  vTaskDelay(pdMS_TO_TICKS(100));

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
