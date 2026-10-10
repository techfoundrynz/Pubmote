#include "config.h"
#include "display/panel.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "remote/display.h"
#include "remote/input_router.h"
#include "remote/powermanagement.h"
#include "remote/remoteinputs.h"
#include "remote/settings_snapshot.h"
#include "slint-esp.h"
#include "ui/display_runtime.h"
#include "ui/navigation.h"
#include "ui/slint_window.h"
#include "utilities/mem_debug.h"
#include <atomic>
static const char *TAG = "PUBREMOTE-SLINT-DISPLAY";
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

static std::atomic<bool> ui_platform_ready(false);

extern "C"
{
  extern volatile uint32_t slint_esp_prepare_us;
  extern volatile uint32_t slint_esp_render_us;
  extern volatile uint32_t slint_esp_flush_us;
  extern volatile uint32_t slint_esp_dirty_px;
}

extern "C" uint32_t display_get_frame_count(void) {
  return slint_esp_frame_counter;
}
bool ui_display_ready(void) {
  return ui_platform_ready.load();
}

static void slint_event_loop(void *pvParameters) {
  ESP_LOGI(TAG, "Slint task started");

  // Initialize Slint platform with our configuration
  SlintPlatformConfiguration<slint::platform::Rgb565BigEndianPixel> config;
  config.size = slint::PhysicalSize(slint::Size<uint32_t>{(uint32_t)HOR_RES, (uint32_t)VER_RES});
  config.panel_handle = panel_handle();
  config.touch_handle = panel_touch_handle();
  config.touch_release_callback = reset_sleep_timer;
  config.byte_swap = false;

  // config.buffer1/buffer2 are deliberately left unset: that selects render_by_line
  // chunked mode, which stages into slint_chunk_buffer in internal SRAM. Full-frame
  // buffers would have to live in PSRAM, and reading them back per frame is slower than
  // rendering into SRAM chunks on this panel.
  ESP_LOGI(TAG, "Using internal SRAM chunked rendering mode");

  // Map rotation
  switch (settings_get_device().screen_rotation) {
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
  ui_platform_ready.store(true);

  ESP_LOGI(TAG, "Creating AppWindow...");
  MEM_MARK("pre AppWindow");
  slint_window = AppWindow::create();
  MEM_MARK("post AppWindow");

  ui_navigation_init(esp_reset_reason());
  MEM_MARK("post callbacks");

  ESP_LOGI(TAG, "Applying initial backlight level: 0");
  display_set_bl_level(0);
  vTaskDelay(pdMS_TO_TICKS(350));
  ESP_LOGI(TAG, "Restoring target backlight level: %d", settings_get_device().bl_level);
  display_set_bl_level(settings_get_device().bl_level);
  if (settings_get_device().hbm_mode == HBM_MODE_ON) {
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
  ui_navigation_shutdown();
  // Released here rather than by whoever asked us to quit: AppWindow owns slint::Timers and
  // those may only be destroyed on this thread.
  slint_window.reset();
  esp_task_wdt_delete(NULL);
  slint_task_handle = NULL;
  vTaskDelete(NULL);
}

static void slint_input_task(void *pvParameters) {

#if SHOW_FPS
  static uint32_t last_fps_time = 0;
  static uint32_t last_frame_count = 0;
#endif

  while (true) {
    if (slint_window) {
      input_router_poll_stick(remote_data.js_x, remote_data.js_y);

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
  if (panel_is_initialized()) {
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
  ESP_ERROR_CHECK(panel_init(on_lcd_color_trans_done, nullptr));

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
  ui_navigation_prepare_shutdown();

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

  panel_deinit();
}
