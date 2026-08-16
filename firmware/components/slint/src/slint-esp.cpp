// Copyright © SixtyFPS GmbH <info@slint.dev>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Slint-Royalty-free-2.0 OR LicenseRef-Slint-Software-3.0

#include "slint-esp.h"
#include "esp_attr.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "slint-platform.h"
#include <cstring>
#include <deque>
#include <mutex>
#if __has_include("soc/soc_caps.h")
  #include "soc/soc_caps.h"
#endif
#if 0 // disabled for SPI panel compatibility
  #include "esp_lcd_panel_rgb.h"
#endif
#include "esp_log.h"
#if __has_include("config.h")
  #include "config.h"
#endif

static const char *TAG = "slint_platform";

// Defined in display.cpp. Serialises panel IO against the brightness, HBM and sleep commands
// other tasks issue on the same esp_lcd handle.
extern "C" bool panel_io_lock_acquire(uint32_t timeout_ms);
extern "C" void panel_io_lock_release();

// Interrupt-driven touch: the ISR wakes the event loop so a touch report is
// processed immediately instead of on the next 20ms poll tick.
static TaskHandle_t s_touch_notify_task = nullptr;
static volatile bool s_touch_event_pending = false;

static void IRAM_ATTR touch_interrupt_callback(esp_lcd_touch_handle_t) {
  s_touch_event_pending = true;
  BaseType_t high_task_wakeup = pdFALSE;
  if (s_touch_notify_task) {
    vTaskNotifyGiveFromISR(s_touch_notify_task, &high_task_wakeup);
  }
  if (high_task_wakeup == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

volatile uint32_t slint_esp_frame_counter = 0;
volatile uint32_t slint_esp_last_frame_us = 0;
// Per-frame breakdown for the on-screen overlay. prepare is scene building, render is the
// per-line rasterisation, flush is the byte swap plus waiting on the panel transfer.
volatile uint32_t slint_esp_prepare_us = 0;
volatile uint32_t slint_esp_render_us = 0;
volatile uint32_t slint_esp_flush_us = 0;
// Time spent inside esp_lcd_panel_draw_bitmap, to find out whether the unattributed
// per-frame cost is the per-chunk address-window command sequence.
static uint64_t g_drawcall_us = 0;
static uint64_t g_drawcall_accum = 0;
static uint32_t g_drawcall_count = 0;

// Phase timestamps from inside the renderer's prepare_scene. 0=entry, 1=before the item
// walk, 2=after it, 3=before Scene::new, 4=after.

static inline esp_err_t timed_draw_bitmap(esp_lcd_panel_handle_t p, int x0, int y0, int x1, int y1,
                                          const void *data) {
  uint64_t d0 = esp_timer_get_time();
  esp_err_t err = esp_lcd_panel_draw_bitmap(p, x0, y0, x1, y1, data);
  g_drawcall_us += esp_timer_get_time() - d0;
  g_drawcall_count++;
  return err;
}
volatile uint32_t slint_esp_dirty_px = 0;

using RepaintBufferType = slint::platform::SoftwareRenderer::RepaintBufferType;

class EspWindowAdapter : public slint::platform::WindowAdapter {
public:
  slint::platform::SoftwareRenderer m_renderer;
  bool needs_redraw = true;
  const slint::PhysicalSize m_size;

  explicit EspWindowAdapter(RepaintBufferType buffer_type, slint::PhysicalSize size)
      : m_renderer(buffer_type), m_size(size) {
  }

  slint::platform::AbstractRenderer &renderer() override {
    return m_renderer;
  }

  slint::PhysicalSize size() override {
    return m_size;
  }

  void request_redraw() override {
    needs_redraw = true;
  }
};

template <typename PixelType> struct EspPlatform : public slint::platform::Platform {
  static inline EspPlatform *active_platform = nullptr;
  static inline void (*set_rotation_callback)(void *, slint::platform::SoftwareRenderer::RenderingRotation) = nullptr;

  EspPlatform(const SlintPlatformConfiguration<PixelType> &config)
      : size(config.size), panel_handle(config.panel_handle), touch_handle(config.touch_handle),
        byte_swap(config.byte_swap), rotation(config.rotation) {
    task = xTaskGetCurrentTaskHandle();
    active_platform = this;
    set_rotation_callback = [](void *instance, slint::platform::SoftwareRenderer::RenderingRotation rot) {
      auto self = reinterpret_cast<EspPlatform<PixelType> *>(instance);
      self->rotation = rot;
      if (self->m_window) {
        self->m_window->m_renderer.set_rendering_rotation(rot);
        self->m_window->needs_redraw = true;
        xTaskNotifyGive(task);
      }
    };
  }

  std::unique_ptr<slint::platform::WindowAdapter> create_window_adapter() override;

  std::chrono::milliseconds duration_since_start() override;
  void run_event_loop() override;
  void quit_event_loop() override;
  void run_in_event_loop(Task) override;

private:
  slint::PhysicalSize size;
  esp_lcd_panel_handle_t panel_handle;
  esp_lcd_touch_handle_t touch_handle;
  bool byte_swap;
  slint::platform::SoftwareRenderer::RenderingRotation rotation;
  class EspWindowAdapter *m_window = nullptr;

  // Need to be static because we can't pass user data to the touch interrupt callback
  static TaskHandle_t task;
  std::mutex queue_mutex;
  std::deque<slint::platform::Platform::Task> queue; // protected by queue_mutex
  bool quit = false;                                 // protected by queue_mutex
};

template <typename PixelType>
std::unique_ptr<slint::platform::WindowAdapter> EspPlatform<PixelType>::create_window_adapter() {
  if (m_window != nullptr) {
    ESP_LOGI(TAG, "FATAL: create_window_adapter called multiple times");
    return nullptr;
  }

  // Chunked line-by-line rendering reuses one buffer; there is no second frame buffer
  // to swap with.
  auto window = std::make_unique<EspWindowAdapter>(RepaintBufferType::ReusedBuffer, size);
  m_window = window.get();
  m_window->m_renderer.set_rendering_rotation(rotation);
  return window;
}

template <typename PixelType> std::chrono::milliseconds EspPlatform<PixelType>::duration_since_start() {
  auto ticks = xTaskGetTickCount();
  return std::chrono::milliseconds(pdTICKS_TO_MS(ticks));
}

#if 0 // disabled for SPI panel compatibility
static SemaphoreHandle_t sem_vsync_end;
static SemaphoreHandle_t sem_gui_ready;

extern "C" bool on_vsync_event(esp_lcd_panel_handle_t panel,
                               const esp_lcd_rgb_panel_event_data_t *edata, void *)
{
    BaseType_t high_task_awoken = pdFALSE;
    if (xSemaphoreTakeFromISR(sem_gui_ready, &high_task_awoken) == pdTRUE) {
        xSemaphoreGiveFromISR(sem_vsync_end, &high_task_awoken);
    }
    return high_task_awoken == pdTRUE;
}
#endif

namespace
{
void byte_swap_color(slint::platform::Rgb565Pixel *pixel) {
  // Swap endianness to big endian
  auto px = reinterpret_cast<uint16_t *>(pixel);
  *px = (*px << 8) | (*px >> 8);
}
void byte_swap_color(slint::Rgb8Pixel *pixel) {
  std::swap(pixel->r, pixel->b);
}
// Already in panel byte order - nothing to do. Present so the templated flush path
// compiles unchanged; config.byte_swap should be false for this pixel type.
void byte_swap_buffer(slint::platform::Rgb565BigEndianPixel *, std::size_t) {
}

void byte_swap_buffer(slint::platform::Rgb565Pixel *ptr, std::size_t len) {
  std::size_t i = 0;
  uint16_t *p16 = reinterpret_cast<uint16_t *>(ptr);
  if (len > 0 && ((uintptr_t)p16 % 4 != 0)) {
    uint16_t val = p16[0];
    p16[0] = (val << 8) | (val >> 8);
    i = 1;
  }
  std::size_t len32 = (len - i) / 2;
  uint32_t *ptr32 = reinterpret_cast<uint32_t *>(p16 + i);
  std::size_t col = 0;
  for (; col + 3 < len32; col += 4) {
    uint32_t v0 = ptr32[col];
    uint32_t v1 = ptr32[col + 1];
    uint32_t v2 = ptr32[col + 2];
    uint32_t v3 = ptr32[col + 3];

    ptr32[col] = ((v0 & 0xFF00FF00) >> 8) | ((v0 & 0x00FF00FF) << 8);
    ptr32[col + 1] = ((v1 & 0xFF00FF00) >> 8) | ((v1 & 0x00FF00FF) << 8);
    ptr32[col + 2] = ((v2 & 0xFF00FF00) >> 8) | ((v2 & 0x00FF00FF) << 8);
    ptr32[col + 3] = ((v3 & 0xFF00FF00) >> 8) | ((v3 & 0x00FF00FF) << 8);
  }
  for (; col < len32; ++col) {
    uint32_t val = ptr32[col];
    ptr32[col] = ((val & 0xFF00FF00) >> 8) | ((val & 0x00FF00FF) << 8);
  }
  if (i + col * 2 < len) {
    std::size_t last_idx = len - 1;
    uint16_t val = p16[last_idx];
    p16[last_idx] = (val << 8) | (val >> 8);
  }
}
void byte_swap_buffer(slint::Rgb8Pixel *ptr, std::size_t len) {
  for (std::size_t i = 0; i < len; ++i) {
    std::swap(ptr[i].r, ptr[i].b);
  }
}
} // namespace

template <typename PixelType> void EspPlatform<PixelType>::run_event_loop() {
  esp_lcd_panel_disp_on_off(panel_handle, true);

  TickType_t max_ticks_to_wait = portMAX_DELAY;
  bool touch_interrupt = false;

  if (touch_handle) {
    // Use the touch controller's interrupt pin when the board has one wired up
    // (int_gpio_num is only set for targets that define TP_INT).
    if (touch_handle->config.int_gpio_num != GPIO_NUM_NC) {
      s_touch_notify_task = task;
      if (esp_lcd_touch_register_interrupt_callback(touch_handle, touch_interrupt_callback) == ESP_OK) {
        touch_interrupt = true;
        ESP_LOGI(TAG, "Touch interrupt enabled on GPIO %d", touch_handle->config.int_gpio_num);
      }
    }
    if (!touch_interrupt) {
      // Fall back to polling (every 30ms) to avoid CPU starvation from continuous interrupts
      // or interrupt storms on boards with floating touch interrupt pins.
      max_ticks_to_wait = pdMS_TO_TICKS(30);
    }
  }

  float last_touch_x = 0;
  float last_touch_y = 0;
  bool touch_down = false;

  while (true) {
#ifdef TARGET_FPS
    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
#endif

    slint::platform::update_timers_and_animations();

    std::optional<slint::platform::Platform::Task> event;
    {
      std::unique_lock lock(queue_mutex);
      if (queue.empty()) {
        if (quit) {
          quit = false;
          break;
        }
      }
      else {
        event = std::move(queue.front());
        queue.pop_front();
      }
    }
    if (event) {
      std::move(*event).run();
      event.reset();
      continue;
    }

    if (m_window) {

      bool read_touch = false;
      if (touch_handle) {
        if (touch_interrupt) {
          // Read on every interrupt; while a touch is active also read each
          // iteration since some controllers don't pulse INT on release.
          if (s_touch_event_pending) {
            s_touch_event_pending = false;
            read_touch = true;
          }
          read_touch = read_touch || touch_down;
        }
        else {
          static uint32_t last_touch_poll = 0;
          uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
          if (now_ms - last_touch_poll >= 20) {
            last_touch_poll = now_ms;
            read_touch = true;
          }
        }
      }
      if (read_touch) {
        uint16_t touchpad_x[1] = {0};
        uint16_t touchpad_y[1] = {0};
        uint8_t touchpad_cnt = 0;

        /* Read touch controller data */
        esp_lcd_touch_read_data(touch_handle);

        /* Get coordinates */
        bool touchpad_pressed =
            esp_lcd_touch_get_coordinates(touch_handle, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

        if (touchpad_pressed && touchpad_cnt > 0) {
          float raw_x = touchpad_x[0];
          float raw_y = touchpad_y[0];
          float rotated_x = raw_x;
          float rotated_y = raw_y;

          switch (rotation) {
          case slint::platform::SoftwareRenderer::RenderingRotation::Rotate90:
            rotated_x = raw_y;
            rotated_y = float(size.width) - raw_x;
            break;
          case slint::platform::SoftwareRenderer::RenderingRotation::Rotate180:
            rotated_x = float(size.width) - raw_x;
            rotated_y = float(size.height) - raw_y;
            break;
          case slint::platform::SoftwareRenderer::RenderingRotation::Rotate270:
            rotated_x = float(size.height) - raw_y;
            rotated_y = raw_x;
            break;
          default:
            break;
          }

          auto scale_factor = m_window->window().scale_factor();
          // ESP_LOGI(TAG, "x: %i, y: %i", touchpad_x[0], touchpad_y[0]);
          last_touch_x = rotated_x / scale_factor;
          last_touch_y = rotated_y / scale_factor;
          m_window->window().dispatch_pointer_move_event(slint::LogicalPosition({last_touch_x, last_touch_y}));
          if (!touch_down) {
            m_window->window().dispatch_pointer_press_event(slint::LogicalPosition({last_touch_x, last_touch_y}),
                                                            slint::PointerEventButton::Left);
          }
          touch_down = true;
        }
        else if (touch_down) {
          m_window->window().dispatch_pointer_release_event(slint::LogicalPosition({last_touch_x, last_touch_y}),
                                                            slint::PointerEventButton::Left);
          m_window->window().dispatch_pointer_exit_event();
          touch_down = false;
        }
      }

      // Held for the whole frame, not per draw call: tx_color is queued DMA that outlives
      // draw_bitmap, so a foreign tx_param between chunks would still race the bus lock and the
      // driver's in-flight bookkeeping. If it is not free the frame stays pending and retries.
      if (m_window->needs_redraw && panel_io_lock_acquire(1000)) {
        m_window->needs_redraw = false;
        uint64_t t_start = esp_timer_get_time();
        uint64_t t_render = 0;
        uint64_t t_copy = 0;
        g_drawcall_us = 0;
        uint64_t t_wait_transmit = 0;
        uint64_t t_prepare = 0;
        uint32_t frame_lines = 0;

        extern SemaphoreHandle_t trans_sem;

        extern uint16_t *slint_chunk_buffer[SLINT_CHUNK_ACCUMULATORS];
        extern int slint_chunk_lines; // resolved at init in display.cpp

        // One accumulator per dirty rectangle rather than one for the whole frame.
        //
        // render_by_line issues a callback per region rectangle per line, all with the same
        // line_y, so when two rectangles overlap vertically the callbacks interleave. A single
        // accumulator keyed on "same x range, next line" is broken by every switch, which both
        // multiplies draw calls - 93 of them for 283 callbacks, measured - and flushes windows
        // one line tall, whose odd row bounds this panel will not accept. Giving each
        // rectangle its own accumulator makes every chunk contiguous in y within its
        // rectangle, so windows inherit the region's even alignment and draw calls collapse to
        // the number of chunks actually needed.
        struct Accumulator {
          std::size_t x0 = 0, x1 = 0;
          std::size_t y0 = 0;
          int lines = 0;
        };
        Accumulator acc[SLINT_CHUNK_ACCUMULATORS];
        // Buffers whose transfer is still outstanding, oldest first. Transfers complete in
        // the order they were queued, so retiring the front on each completion is enough.
        int inflight_q[SLINT_CHUNK_ACCUMULATORS];
        int q_head = 0, q_len = 0;

        // Bounded, not portMAX_DELAY: this runs on the event loop, and the task watchdog is
        // fed from a Slint timer, so blocking here forever takes the UI down with it. A frame
        // is tens of milliseconds, so a completion this late is a lost one - drop it, say so,
        // and keep the loop alive.
        auto retire_one = [&]() {
          uint64_t wait_start = esp_timer_get_time();
          if (xSemaphoreTake(trans_sem, pdMS_TO_TICKS(250)) != pdTRUE) {
            ESP_LOGW(TAG, "panel transfer completion timed out; dropping it");
          }
          t_wait_transmit += esp_timer_get_time() - wait_start;
          q_head = (q_head + 1) % SLINT_CHUNK_ACCUMULATORS;
          q_len--;
        };

        // draw_bitmap opens with a CASET/RASET tx_param, and esp_lcd's tx_param acquires the SPI
        // bus, which acquire_core() only grants once the queue is empty - so a draw call already
        // waits for every outstanding transfer, just inside spi_device_acquire_bus at
        // portMAX_DELAY. Retiring here costs the same wall clock and stays bounded.
        auto drain_all = [&]() {
          if (!trans_sem) {
            return;
          }
          while (q_len > 0) {
            retire_one();
          }
        };

        // Rendering keeps going while the panel drains, so only stall when the line about to be
        // written belongs to a buffer still being read. Transfers retire oldest first.
        auto wait_for = [&](int buf) {
          if (!trans_sem) {
            return;
          }
          for (int i = 0; i < q_len; i++) {
            if (inflight_q[(q_head + i) % SLINT_CHUNK_ACCUMULATORS] == buf) {
              for (int r = 0; r <= i; r++) {
                retire_one();
              }
              return;
            }
          }
        };

        auto flush = [&](int a) {
          Accumulator &c = acc[a];
          if (c.lines <= 0) {
            return;
          }
          std::size_t width = c.x1 - c.x0;
          PixelType *base = reinterpret_cast<PixelType *>(slint_chunk_buffer[a]);

          uint64_t chunk_copy_start = esp_timer_get_time();
          if (byte_swap) {
            byte_swap_buffer(base, width * c.lines);
          }
          t_copy += esp_timer_get_time() - chunk_copy_start;

          drain_all();
          // Only count a transfer that was actually queued. A failed call raises no completion
          // callback, and waiting for one that cannot arrive wedges the event loop for good.
          if (timed_draw_bitmap(panel_handle, c.x0, c.y0, c.x1, c.y0 + c.lines, slint_chunk_buffer[a]) == ESP_OK) {
            inflight_q[(q_head + q_len) % SLINT_CHUNK_ACCUMULATORS] = a;
            q_len++;
          }
          else {
            ESP_LOGW(TAG, "draw_bitmap failed for chunk %d,%d %dx%d", (int)c.x0, (int)c.y0,
                     (int)(c.x1 - c.x0), c.lines);
          }
          c.lines = 0;
        };

        // Slint runs prepare_scene (item tree walk, layout, text measurement,
        // dirty region computation) before it invokes the first line callback,
        // so the gap to that first call isolates it from the per-line loop.
        uint64_t t_first_line = 0;
        uint32_t line_callbacks = 0;

        auto region = m_window->m_renderer.render_by_line<PixelType>(
            [&](std::size_t line_y, std::size_t line_start, std::size_t line_end, auto &&render_fn) {
              if (t_first_line == 0) {
                t_first_line = esp_timer_get_time();
              }
              line_callbacks++;

              // The dirty region already arrives rounded out to even coordinates, so the window
              // needs no widening here and no pixel is ever invented.
              const std::size_t span_x0 = line_start;
              const std::size_t span_x1 = line_end;

              // Continue whichever accumulator this line extends; otherwise take a free one, and
              // if none is free flush the fullest to make room.
              int a = -1;
              for (int i = 0; i < SLINT_CHUNK_ACCUMULATORS; i++) {
                if (acc[i].lines > 0 && acc[i].x0 == span_x0 && acc[i].x1 == span_x1 &&
                    acc[i].y0 + acc[i].lines == line_y) {
                  a = i;
                  break;
                }
              }
              if (a < 0) {
                for (int i = 0; i < SLINT_CHUNK_ACCUMULATORS; i++) {
                  if (acc[i].lines == 0) {
                    a = i;
                    break;
                  }
                }
                if (a < 0) {
                  a = 0;
                  for (int i = 1; i < SLINT_CHUNK_ACCUMULATORS; i++) {
                    if (acc[i].lines > acc[a].lines) {
                      a = i;
                    }
                  }
                  flush(a);
                }
              }
              if (acc[a].lines == slint_chunk_lines) {
                flush(a);
              }
              if (acc[a].lines == 0) {
                acc[a].x0 = span_x0;
                acc[a].x1 = span_x1;
                acc[a].y0 = line_y;
              }

              // The staging buffer must not be written while its own transfer is still reading it.
              wait_for(a);

              std::size_t width = acc[a].x1 - acc[a].x0;
              PixelType *row = reinterpret_cast<PixelType *>(slint_chunk_buffer[a]) + acc[a].lines * width;

              // Deliberately untimed. Timing each line cost two esp_timer_get_time() calls per
              // scanline - 932 per full-screen frame - which is milliseconds of measurement
              // overhead in the frame it is measuring. t_render is derived below instead.
              render_fn(std::span<PixelType>{row, width});

              acc[a].lines++;
            });

        for (int a = 0; a < SLINT_CHUNK_ACCUMULATORS; a++) {
          flush(a);
        }

        // Everything between the first line callback and here that was not staging,
        // waiting on the panel or issuing a draw call.
        if (t_first_line != 0) {
          uint64_t spent = esp_timer_get_time() - t_first_line;
          uint64_t overhead = t_copy + t_wait_transmit + g_drawcall_us;
          t_render = spent > overhead ? spent - overhead : 0;
        }
        t_prepare = t_first_line ? (t_first_line - t_start) : 0;
        frame_lines = line_callbacks;

        if (trans_sem) {
          while (q_len > 0) {
            retire_one();
          }
        }

        // How much of the panel Slint asked to be repainted this frame. Cheap to total up,
        // and it is the number that says whether invalidation or pixel cost is the limit:
        // rectangle count drives callbacks and panel transactions, area drives rasterisation
        // and bytes on the wire.
        uint32_t last_dirty_rects = 0;
        uint32_t last_dirty_px = 0;
        for (auto [o, s] : region.rectangles()) {
          last_dirty_rects++;
          last_dirty_px += (uint32_t)s.width * (uint32_t)s.height;
        }

        uint64_t t_total = esp_timer_get_time() - t_start;

        // The single source of truth for frame rate: one increment per
        // frame actually drawn, plus how long that draw took. Anything
        // that displays or logs FPS derives it from these rather than
        // measuring something of its own.
        slint_esp_last_frame_us = (uint32_t)t_total;
        // Not ++: compound ops on volatile are deprecated in C++20
        slint_esp_frame_counter = slint_esp_frame_counter + 1;

        // The same split the perf log prints, published for the on-screen overlay:
        // reading it off the panel does not need a serial monitor attached, and the
        // monitor holds firmware.elf open for the exception decoder anyway.
        slint_esp_prepare_us = (uint32_t)t_prepare;
        slint_esp_render_us = (uint32_t)t_render;
        slint_esp_flush_us = (uint32_t)(t_copy + t_wait_transmit);
        slint_esp_dirty_px = last_dirty_px;

#if SLINT_PERF_LOG
        static uint64_t accum_render = 0;
        static uint64_t accum_copy = 0;
        static uint64_t accum_wait_transmit = 0;
        static uint64_t accum_total = 0;
        static uint64_t accum_prepare = 0;
        static uint32_t accum_lines = 0;
        static uint32_t accum_rects = 0;
        static uint32_t accum_dirty = 0;
        static uint32_t peak_dirty = 0;
        static int frame_count = 0;

        accum_render += t_render;
        accum_copy += t_copy;
        g_drawcall_accum += g_drawcall_us;
        accum_wait_transmit += t_wait_transmit;
        accum_total += t_total;
        accum_prepare += t_prepare;
        accum_lines += frame_lines;
        accum_rects += last_dirty_rects;
        accum_dirty += last_dirty_px;
        if (last_dirty_px > peak_dirty) {
          peak_dirty = last_dirty_px;
        }
        frame_count++;

        if (frame_count >= 60) {
          // "other" is what remains once scene prep and the measured flush work
          // are removed: per-line loop overhead outside render_fn.
          long long avg_total = accum_total / 60;
          long long avg_prepare = accum_prepare / 60;
          long long avg_other = avg_total - avg_prepare - (long long)(accum_render / 60) -
                                (long long)(accum_copy / 60) - (long long)(accum_wait_transmit / 60);
          uint32_t screen = (uint32_t)size.width * (uint32_t)size.height;
          ESP_LOGI(TAG,
                   "Slint [60f avg]: %lld us/frame (%.1f fps) = prepare %lld + render %lld + copy %lld "
                   "+ wait %lld + other %lld | dirty %.1f%% (peak %.1f%%) in %.1f rects, %lu lines, "
                   "%lu drawcalls costing %lld us",
                   avg_total, avg_total > 0 ? 1000000.0f / (float)avg_total : 0.0f, avg_prepare, accum_render / 60,
                   accum_copy / 60, accum_wait_transmit / 60, avg_other,
                   100.0f * (float)(accum_dirty / 60) / (float)screen, 100.0f * (float)peak_dirty / (float)screen,
                   accum_rects / 60.0f, (unsigned long)(accum_lines / 60), (unsigned long)(g_drawcall_count / 60),
                   (long long)(g_drawcall_accum / 60));
          accum_rects = 0;
          accum_dirty = 0;
          peak_dirty = 0;
          accum_render = 0;
          accum_copy = 0;
          accum_wait_transmit = 0;
          accum_total = 0;
          accum_prepare = 0;
          accum_lines = 0;
          g_drawcall_accum = 0;
          g_drawcall_count = 0;
          frame_count = 0;
        }
#else
        (void)t_prepare;
        (void)frame_lines;
        (void)t_render;
        (void)t_copy;
        (void)t_wait_transmit;
#endif
        panel_io_lock_release();
      }

      if (m_window->window().has_active_animations()) {
#ifdef TARGET_FPS
        uint32_t end_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t elapsed = end_time - start_time;
        uint32_t target_time = 1000 / TARGET_FPS;
        if (elapsed < target_time) {
          vTaskDelay(pdMS_TO_TICKS(target_time - elapsed));
        }
        else {
          vTaskDelay(pdMS_TO_TICKS(1)); // Yield to IDLE to feed task watchdog
        }
#else
        vTaskDelay(pdMS_TO_TICKS(1)); // Yield to IDLE to feed task watchdog
#endif
        continue;
      }
    }

    TickType_t ticks_to_wait = max_ticks_to_wait;
    if (touch_interrupt && touch_down) {
      // Poll for the release while a touch is active (see read_touch above)
      ticks_to_wait = std::min(ticks_to_wait, (TickType_t)pdMS_TO_TICKS(30));
    }
    if (auto wait_time = slint::platform::duration_until_next_timer_update()) {
      ticks_to_wait = std::min(ticks_to_wait, pdMS_TO_TICKS(wait_time->count()));
    }
    // ESP_LOGI("SLINT-ESP-LOOP", "Waiting: touch=%p, max_ticks=%u, ticks_to_wait=%u",
    //          touch_handle, (unsigned int)max_ticks_to_wait, (unsigned int)ticks_to_wait);
    uint32_t notified = ulTaskNotifyTake(/*reset to zero*/ pdTRUE, ticks_to_wait);

    if (notified > 0) {
      // If we received a notification without blocking (e.g., events were queued while we were drawing),
      // yield to the IDLE task for 1 tick. This prevents the UI task from running continuously
      // at 100% CPU and triggering the task watchdog.
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  vTaskDelete(NULL);
}

template <typename PixelType> void EspPlatform<PixelType>::quit_event_loop() {
  {
    const std::unique_lock lock(queue_mutex);
    quit = true;
  }
  if (xPortInIsrContext()) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  }
  else {
    xTaskNotifyGive(task);
  }
}

template <typename PixelType> void EspPlatform<PixelType>::run_in_event_loop(slint::platform::Platform::Task event) {
  {
    const std::unique_lock lock(queue_mutex);
    queue.push_back(std::move(event));
  }
  if (xPortInIsrContext()) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  }
  else {
    xTaskNotifyGive(task);
  }
}

template <typename PixelType> TaskHandle_t EspPlatform<PixelType>::task = {};

void slint_esp_init(const SlintPlatformConfiguration<slint::platform::Rgb565Pixel> &config) {
  slint::platform::set_platform(std::make_unique<EspPlatform<slint::platform::Rgb565Pixel>>(config));
}

void slint_esp_init(const SlintPlatformConfiguration<slint::platform::Rgb565BigEndianPixel> &config) {
  slint::platform::set_platform(std::make_unique<EspPlatform<slint::platform::Rgb565BigEndianPixel>>(config));
}

void slint_esp_init(const SlintPlatformConfiguration<slint::Rgb8Pixel> &config) {
  slint::platform::set_platform(std::make_unique<EspPlatform<slint::Rgb8Pixel>>(config));
}

void slint_esp_set_rotation(slint::platform::SoftwareRenderer::RenderingRotation rotation) {
  if (EspPlatform<slint::platform::Rgb565Pixel>::active_platform &&
      EspPlatform<slint::platform::Rgb565Pixel>::set_rotation_callback) {
    EspPlatform<slint::platform::Rgb565Pixel>::set_rotation_callback(
        EspPlatform<slint::platform::Rgb565Pixel>::active_platform, rotation);
  }
  else if (EspPlatform<slint::Rgb8Pixel>::active_platform && EspPlatform<slint::Rgb8Pixel>::set_rotation_callback) {
    EspPlatform<slint::Rgb8Pixel>::set_rotation_callback(EspPlatform<slint::Rgb8Pixel>::active_platform, rotation);
  }
}

// Force recompile for deferred byte swap
