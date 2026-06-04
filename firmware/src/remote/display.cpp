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
#include "remoteinputs.h"
#include "settings.h"

// Slint inclusion
#include "esp_heap_caps.h"
#include "generated/app-window.h"
#include "slint-esp.h"

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
#define MAX_TRAN_SIZE (HOR_RES * VER_RES * sizeof(uint16_t))

static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;
static bool is_initialized = false;
static uint8_t bl_level = 0;

uint16_t *slint_chunk_buffer[2] = {NULL, NULL};
extern const int slint_chunk_lines = 20;

// PSRAM frame buffers removed for pure chunked mode

static SlintWindowPtr slint_window;
static TaskHandle_t slint_task_handle = NULL;
static TaskHandle_t slint_input_task_handle = NULL;

SlintWindowPtr get_slint_window() {
  return slint_window;
}

#include <atomic>

#ifdef SHOW_FPS
static std::atomic<int> g_loop_count(0);
#endif

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
extern "C"
{
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

#include <cmath>
#include <algorithm>

static slint::Image generate_arc(float size, float thickness, float track_start_deg, float track_span_deg, float ind_start_deg, float ind_span_deg, slint::Brush track_brush, slint::Brush ind_brush) {
    int w = std::max(1, (int)std::ceil(size));
    int h = std::max(1, (int)std::ceil(size));
    
    slint::SharedPixelBuffer<slint::Rgba8Pixel> buffer(w, h);
    auto *data = buffer.begin();
    
    float cx = size / 2.0f;
    float cy = size / 2.0f;
    float radius = (size - thickness) / 2.0f;
    float half_t = thickness / 2.0f;
    
    slint::Color t_color = track_brush.color();
    slint::Color i_color = ind_brush.color();
    
    float pi = 3.14159265359f;
    auto to_rad = [pi](float deg) { return deg * pi / 180.0f; };
    float track_s = to_rad(track_start_deg);
    float track_e = to_rad(track_start_deg + track_span_deg);
    float ind_s = to_rad(ind_start_deg);
    float ind_e = to_rad(ind_start_deg + ind_span_deg);
    
    float cap_trk_a_x = cx + radius * std::cos(track_s);
    float cap_trk_a_y = cy + radius * std::sin(track_s);
    float cap_trk_b_x = cx + radius * std::cos(track_e);
    float cap_trk_b_y = cy + radius * std::sin(track_e);
    
    float cap_ind_a_x = cx + radius * std::cos(ind_s);
    float cap_ind_a_y = cy + radius * std::sin(ind_s);
    float cap_ind_b_x = cx + radius * std::cos(ind_e);
    float cap_ind_b_y = cy + radius * std::sin(ind_e);

    auto angular_diff = [pi](float a, float b) {
        float d = std::fmod(a - b, 2 * pi);
        if (d < 0) d += 2 * pi;
        return d;
    };
    
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float dx = (x + 0.5f) - cx;
            float dy = (y + 0.5f) - cy;
            float dist = std::sqrt(dx * dx + dy * dy);
            float d_center = std::abs(dist - radius);
            float alpha_radial = std::clamp(half_t - d_center + 0.5f, 0.0f, 1.0f);
            
            if (alpha_radial > 0.0f) {
                float angle = std::atan2(dy, dx);
                if (angle < 0) angle += 2 * pi;
                
                auto check_arc = [&](float a_start, float a_end, float span, float c_ax, float c_ay, float c_bx, float c_by) -> float {
                    if (span <= 0) return 0.0f;
                    float rel_angle = angular_diff(angle, a_start);
                    
                    if (rel_angle <= to_rad(span)) {
                        return alpha_radial;
                    } else {
                        float dx_a = (x + 0.5f) - c_ax;
                        float dy_a = (y + 0.5f) - c_ay;
                        float d_cap_a = std::sqrt(dx_a * dx_a + dy_a * dy_a);
                        
                        float dx_b = (x + 0.5f) - c_bx;
                        float dy_b = (y + 0.5f) - c_by;
                        float d_cap_b = std::sqrt(dx_b * dx_b + dy_b * dy_b);
                        
                        float d_cap = std::min(d_cap_a, d_cap_b);
                        return std::clamp(half_t - d_cap + 0.5f, 0.0f, 1.0f);
                    }
                };
                
                float a_ind = check_arc(ind_s, ind_e, ind_span_deg, cap_ind_a_x, cap_ind_a_y, cap_ind_b_x, cap_ind_b_y);
                float a_trk = check_arc(track_s, track_e, track_span_deg, cap_trk_a_x, cap_trk_a_y, cap_trk_b_x, cap_trk_b_y);
                
                uint8_t r = 0, g = 0, b = 0, a = 0;
                
                auto blend = [&](slint::Color c, float alpha) {
                    if (alpha <= 0) return;
                    uint32_t ca = c.alpha() * alpha;
                    if (ca == 0) return;
                    if (a == 0) {
                        r = c.red(); g = c.green(); b = c.blue(); a = ca;
                    } else {
                        uint32_t out_a = ca + a * (255 - ca) / 255;
                        r = (c.red() * ca + r * a * (255 - ca) / 255) / out_a;
                        g = (c.green() * ca + g * a * (255 - ca) / 255) / out_a;
                        b = (c.blue() * ca + b * a * (255 - ca) / 255) / out_a;
                        a = out_a;
                    }
                };
                
                blend(t_color, a_trk);
                blend(i_color, a_ind);
                
                data[y * w + x] = slint::Rgba8Pixel{
                    (uint8_t)(r * a / 255), 
                    (uint8_t)(g * a / 255), 
                    (uint8_t)(b * a / 255), 
                    a
                };
            } else {
                data[y * w + x] = slint::Rgba8Pixel{0, 0, 0, 0};
            }
        }
    }
    
    return slint::Image(buffer);
}

static slint::Image generate_stats_arcs(float size, float scale, float speed_val, float duty_val, bool left_pad, bool right_pad, slint::Brush bg_brush, slint::Brush track_brush, slint::Brush track_dim_brush, slint::Brush accent_brush, slint::Brush text_brush) {
    int w = std::max(1, (int)std::ceil(size));
    int h = std::max(1, (int)std::ceil(size));
    
    slint::SharedPixelBuffer<slint::Rgba8Pixel> buffer(w, h);
    auto *data = buffer.begin();
    
    slint::Color bg = bg_brush.color();
    slint::Color track = track_brush.color();
    slint::Color track_dim = track_dim_brush.color();
    slint::Color accent = accent_brush.color();
    slint::Color text = text_brush.color();
    
    float pi = 3.14159265359f;
    auto to_rad = [pi](float deg) { return deg * pi / 180.0f; };
    
    auto angular_diff = [pi](float a, float b) {
        float d = std::fmod(a - b, 2 * pi);
        if (d < 0) d += 2 * pi;
        return d;
    };
    
    float speed_cx = size / 2.0f;
    float speed_cy = size / 2.0f;
    float speed_t = 16.0f * scale;
    float speed_r = (size - speed_t) / 2.0f;
    float speed_s = to_rad(135.0f);
    float speed_e = to_rad(135.0f + 270.0f);
    float speed_span = 270.0f;
    float speed_ind_span = 270.0f * std::clamp(speed_val, 0.0f, 1.0f);
    float speed_ind_s = speed_s;
    float speed_ind_e = speed_ind_s + to_rad(speed_ind_span);
    
    float duty_size = size * 200.0f / 240.0f;
    float duty_cx = size / 2.0f;
    float duty_cy = size / 2.0f;
    float duty_t = 12.0f * scale;
    float duty_r = (duty_size - duty_t) / 2.0f;
    float duty_s = to_rad(135.0f);
    float duty_e = to_rad(135.0f + 270.0f);
    float duty_span = 270.0f;
    float duty_ind_span = 270.0f * std::clamp(duty_val, 0.0f, 1.0f);
    float duty_ind_s = duty_e - to_rad(duty_ind_span);
    float duty_ind_e = duty_e;
    
    float left_t = 10.0f * scale;
    float left_r = (size - left_t) / 2.0f;
    float left_s = to_rad(93.0f);
    float left_e = to_rad(93.0f + 12.0f);
    float left_span = 12.0f;
    float left_ind_s = left_s;
    float left_ind_e = left_pad ? left_e : left_s;
    float left_ind_span = left_pad ? 12.0f : 0.0f;
    
    float right_t = 10.0f * scale;
    float right_r = (size - right_t) / 2.0f;
    float right_s = to_rad(75.0f);
    float right_e = to_rad(75.0f + 12.0f);
    float right_span = 12.0f;
    float right_ind_s = right_s;
    float right_ind_e = right_pad ? right_e : right_s;
    float right_ind_span = right_pad ? 12.0f : 0.0f;

    struct ArcDef {
        float cx, cy, r, half_t;
        float s, e, span;
        float ind_s, ind_e, ind_span;
        slint::Color track_col, ind_col;
        float cap_trk_a_x, cap_trk_a_y, cap_trk_b_x, cap_trk_b_y;
        float cap_ind_a_x, cap_ind_a_y, cap_ind_b_x, cap_ind_b_y;
    };
    
    auto init_arc = [&](float cx, float cy, float r, float t, float s, float e, float span, float ind_s, float ind_e, float ind_span, slint::Color t_col, slint::Color i_col) -> ArcDef {
        return ArcDef{
            cx, cy, r, t / 2.0f, s, e, span, ind_s, ind_e, ind_span, t_col, i_col,
            cx + r * std::cos(s), cy + r * std::sin(s),
            cx + r * std::cos(e), cy + r * std::sin(e),
            cx + r * std::cos(ind_s), cy + r * std::sin(ind_s),
            cx + r * std::cos(ind_e), cy + r * std::sin(ind_e)
        };
    };
    
    ArcDef arcs[4] = {
        init_arc(speed_cx, speed_cy, speed_r, speed_t, speed_s, speed_e, speed_span, speed_ind_s, speed_ind_e, speed_ind_span, track, accent),
        init_arc(duty_cx, duty_cy, duty_r, duty_t, duty_s, duty_e, duty_span, duty_ind_s, duty_ind_e, duty_ind_span, track_dim, accent),
        init_arc(speed_cx, speed_cy, left_r, left_t, left_s, left_e, left_span, left_ind_s, left_ind_e, left_ind_span, track, text),
        init_arc(speed_cx, speed_cy, right_r, right_t, right_s, right_e, right_span, right_ind_s, right_ind_e, right_ind_span, track, text)
    };
    
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t pr = bg.red(), pg = bg.green(), pb = bg.blue();
            
            auto blend = [&](slint::Color c, float alpha) {
                if (alpha <= 0) return;
                uint32_t ca = c.alpha() * alpha;
                if (ca == 0) return;
                pr = (c.red() * ca + pr * (255 - ca)) / 255;
                pg = (c.green() * ca + pg * (255 - ca)) / 255;
                pb = (c.blue() * ca + pb * (255 - ca)) / 255;
            };
            
            for (int i = 0; i < 4; i++) {
                ArcDef &a = arcs[i];
                float dx = (x + 0.5f) - a.cx;
                float dy = (y + 0.5f) - a.cy;
                float dist = std::sqrt(dx * dx + dy * dy);
                float d_center = std::abs(dist - a.r);
                float alpha_radial = std::clamp(a.half_t - d_center + 0.5f, 0.0f, 1.0f);
                
                if (alpha_radial > 0.0f) {
                    float angle = std::atan2(dy, dx);
                    if (angle < 0) angle += 2 * pi;
                    
                    auto check_arc = [&](float a_start, float a_end, float span, float c_ax, float c_ay, float c_bx, float c_by) -> float {
                        if (span <= 0) return 0.0f;
                        float rel_angle = angular_diff(angle, a_start);
                        if (rel_angle <= to_rad(span)) {
                            return alpha_radial;
                        } else {
                            float dx_a = (x + 0.5f) - c_ax;
                            float dy_a = (y + 0.5f) - c_ay;
                            float d_cap_a = std::sqrt(dx_a * dx_a + dy_a * dy_a);
                            
                            float dx_b = (x + 0.5f) - c_bx;
                            float dy_b = (y + 0.5f) - c_by;
                            float d_cap_b = std::sqrt(dx_b * dx_b + dy_b * dy_b);
                            
                            float d_cap = std::min(d_cap_a, d_cap_b);
                            return std::clamp(a.half_t - d_cap + 0.5f, 0.0f, 1.0f);
                        }
                    };
                    
                    float a_trk = check_arc(a.s, a.e, a.span, a.cap_trk_a_x, a.cap_trk_a_y, a.cap_trk_b_x, a.cap_trk_b_y);
                    float a_ind = check_arc(a.ind_s, a.ind_e, a.ind_span, a.cap_ind_a_x, a.cap_ind_a_y, a.cap_ind_b_x, a.cap_ind_b_y);
                    
                    blend(a.track_col, a_trk);
                    blend(a.ind_col, a_ind);
                }
            }
            
            data[y * w + x] = slint::Rgba8Pixel{pr, pg, pb, 255};
        }
    }
    
    return slint::Image(buffer);
}

static void connect_callbacks() {
  const auto &state = slint_window->global<UiState>();

  state.on_screen_changed([](Screen screen) { cached_active_screen.store(screen); });

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

  const auto &arc_gen = slint_window->global<ArcGenerator>();
  arc_gen.on_generate(generate_arc);

  const auto &stats_arc_gen = slint_window->global<StatsArcGenerator>();
  stats_arc_gen.on_generate(generate_stats_arcs);
}

// Hardware settings apply
extern "C" void apply_theme_settings() {
  const auto &theme = slint_window->global<Theme>();

  // Set the actual panel resolution dynamically based on the current screen size
  theme.set_panel_res(HOR_RES);

// Set the font scale based on SCALE_FONT macro if defined, otherwise panel resolution ratio
#ifdef SCALE_FONT
  theme.set_font_scale(SCALE_FONT);
#else
  theme.set_font_scale((float)HOR_RES / 240.0f);
#endif

  // Custom accent color
  theme.set_accent(slint::Color::from_argb_encoded(device_settings.theme_color));

  // Dark text mode (with dynamic background)
  if (device_settings.dark_text) {
    theme.set_bg(slint::Color::from_rgb_uint8(255, 255, 255));
    theme.set_text(slint::Color::from_rgb_uint8(0, 0, 0));
    theme.set_text_dim(slint::Color::from_rgb_uint8(100, 100, 100));
  }
  else {
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
  config.size = slint::PhysicalSize(slint::Size<uint32_t>{(uint32_t)HOR_RES, (uint32_t)VER_RES});
  config.panel_handle = lcd_panel;
  config.touch_handle = touch_handle;
  config.byte_swap = true; // Swap bytes for standard SPI/QSPI big-endian display interfaces
  
  // Removed buffer1 and buffer2 assignment to force render_by_line chunked rendering

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

#ifdef SHOW_FPS
  static uint32_t last_fps_time = 0;
  static int last_frame_count = 0;
  static std::atomic<bool> loop_ready(true);
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
      int current_dir = 0;
      if (remote_data.js_y > 0.5) {
        current_dir = 1; // Down -> Tab
      }
      else if (remote_data.js_y < -0.5) {
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
            }
            else {
              slint_window->window().dispatch_key_press_event("\x19"); // Backtab
              slint_window->window().dispatch_key_release_event("\x19");
            }
          });
        }
      }
      else {
        last_dir = 0;
      }

#ifdef SHOW_FPS
      // Keep event loop saturated with a single counting payload
      if (loop_ready.exchange(false)) {
        slint::invoke_from_event_loop([]() {
          g_loop_count++;
          loop_ready.store(true);
        });
      }

      // Calculate and send true FPS to Slint every second
      now = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if (now - last_fps_time >= 1000) {
        int current_count = g_loop_count.load();
        int fps = current_count - last_frame_count;
        last_frame_count = current_count;
        last_fps_time = now;
        slint::invoke_from_event_loop([=]() {
          if (slint_window) {
            slint_window->global<UiState>().set_fps(fps);
          }
        });
      }
#endif
    }
#ifdef SHOW_FPS
    vTaskDelay(pdMS_TO_TICKS(1)); // Poll frequently enough to capture high FPS
#else
    vTaskDelay(pdMS_TO_TICKS(30)); // Standard polling rate
#endif
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
      .x_max = HOR_RES,
      .y_max = VER_RES,
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

  // Allocate chunk buffers early to avoid memory fragmentation from the 64KB Slint task stack
  slint_chunk_buffer[0] = (uint16_t *)heap_caps_malloc(HOR_RES * slint_chunk_lines * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  slint_chunk_buffer[1] = (uint16_t *)heap_caps_malloc(HOR_RES * slint_chunk_lines * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!slint_chunk_buffer[0] || !slint_chunk_buffer[1]) {
    ESP_LOGE(TAG, "Failed to allocate early chunk buffers!");
    abort();
  }

  // PSRAM frame buffers removed for pure chunked mode

  trans_sem = xSemaphoreCreateBinary();
  ESP_ERROR_CHECK(app_lcd_init());
#if TOUCH_ENABLED
  ESP_ERROR_CHECK(app_touch_init());
#endif

  is_initialized = true;

  // Start Slint Event Loop Task
  xTaskCreatePinnedToCore(slint_event_loop, "slint_event_loop", 64 * 1024, NULL, 20, &slint_task_handle, 1);

  // Provide global settings
  if (slint_window) {
#ifdef SHOW_FPS
    slint_window->global<UiState>().set_show_fps(true);
#endif
  }

  // Start Input Polling Task (pinned to core 0, leaving core 1 fully dedicated to the Slint event loop)
  xTaskCreatePinnedToCore(slint_input_task, "slint_input_task", 4096, NULL, 20, &slint_input_task_handle, 0);
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
