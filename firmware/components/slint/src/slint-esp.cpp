// Copyright © SixtyFPS GmbH <info@slint.dev>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Slint-Royalty-free-2.0 OR LicenseRef-Slint-Software-3.0

#include <cstring>
#include <deque>
#include <mutex>
#include "slint-esp.h"
#include "slint-platform.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "esp_attr.h"
#if __has_include("soc/soc_caps.h")
#    include "soc/soc_caps.h"
#endif
#if 0 // disabled for SPI panel compatibility
#    include "esp_lcd_panel_rgb.h"
#endif
#include "esp_log.h"
#if __has_include("config.h")
#include "config.h"
#endif

static const char *TAG = "slint_platform";

// Per-frame dirty region summary. Set to 0 to remove entirely.
#ifndef SLINT_DIRTY_LOG
#    define SLINT_DIRTY_LOG 1
#endif



// Interrupt-driven touch: the ISR wakes the event loop so a touch report is
// processed immediately instead of on the next 20ms poll tick.
static TaskHandle_t s_touch_notify_task = nullptr;
static volatile bool s_touch_event_pending = false;

static void IRAM_ATTR touch_interrupt_callback(esp_lcd_touch_handle_t)
{
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
static uint64_t g_phase_us[5] = { 0, 0, 0, 0, 0 };
static uint64_t g_phase_accum[4] = { 0, 0, 0, 0 };
// Phases 5/6 bracket a text shaping pass; they nest inside the walk, so accumulate
// rather than timestamp.
static uint64_t g_shape_enter = 0;
static uint64_t g_shape_us = 0;
static uint64_t g_shape_accum = 0;
static uint32_t g_shape_count = 0;
static uint64_t g_text_enter = 0;
static uint64_t g_text_us = 0;
static uint64_t g_text_accum = 0;
static uint32_t g_text_count = 0;
static uint32_t g_visited = 0;
static uint32_t g_drawn = 0;
static uint64_t g_visited_accum = 0;
static uint64_t g_drawn_accum = 0;
extern "C" void slint_esp_phase_mark(uint32_t phase)
{
    if (phase < 5) {
        g_phase_us[phase] = esp_timer_get_time();
    } else if (phase == 5) {
        g_shape_enter = esp_timer_get_time();
    } else if (phase == 6) {
        g_shape_us += esp_timer_get_time() - g_shape_enter;
        g_shape_count++;
    } else if (phase == 7) {
        g_text_enter = esp_timer_get_time();
    } else if (phase == 8) {
        g_text_us += esp_timer_get_time() - g_text_enter;
        g_text_count++;
    } else if (phase == 9) {
        g_visited++;
    } else if (phase == 10) {
        g_drawn++;
    }
}

static inline void timed_draw_bitmap(esp_lcd_panel_handle_t p, int x0, int y0, int x1, int y1,
                                     const void *data)
{
    uint64_t d0 = esp_timer_get_time();
    esp_lcd_panel_draw_bitmap(p, x0, y0, x1, y1, data);
    g_drawcall_us += esp_timer_get_time() - d0;
    g_drawcall_count++;
}
volatile uint32_t slint_esp_dirty_px = 0;



using RepaintBufferType = slint::platform::SoftwareRenderer::RepaintBufferType;

class EspWindowAdapter : public slint::platform::WindowAdapter
{
public:
    slint::platform::SoftwareRenderer m_renderer;
    bool needs_redraw = true;
    const slint::PhysicalSize m_size;

    explicit EspWindowAdapter(RepaintBufferType buffer_type, slint::PhysicalSize size)
        : m_renderer(buffer_type), m_size(size)
    {
    }

    slint::platform::AbstractRenderer &renderer() override { return m_renderer; }

    slint::PhysicalSize size() override { return m_size; }

    void request_redraw() override { needs_redraw = true; }
};

template<typename PixelType>
struct EspPlatform : public slint::platform::Platform
{
    static inline EspPlatform* active_platform = nullptr;
    static inline void (*set_rotation_callback)(void*, slint::platform::SoftwareRenderer::RenderingRotation) = nullptr;

    EspPlatform(const SlintPlatformConfiguration<PixelType> &config)
        : size(config.size),
          panel_handle(config.panel_handle),
          touch_handle(config.touch_handle),
          byte_swap(config.byte_swap),
          rotation(config.rotation)
    {
        task = xTaskGetCurrentTaskHandle();
        active_platform = this;
        set_rotation_callback = [](void* instance, slint::platform::SoftwareRenderer::RenderingRotation rot) {
            auto self = reinterpret_cast<EspPlatform<PixelType>*>(instance);
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
    bool quit = false; // protected by queue_mutex
};

template<typename PixelType>
std::unique_ptr<slint::platform::WindowAdapter> EspPlatform<PixelType>::create_window_adapter()
{
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

template<typename PixelType>
std::chrono::milliseconds EspPlatform<PixelType>::duration_since_start()
{
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

namespace {
void byte_swap_color(slint::platform::Rgb565Pixel *pixel)
{
    // Swap endianness to big endian
    auto px = reinterpret_cast<uint16_t *>(pixel);
    *px = (*px << 8) | (*px >> 8);
}
void byte_swap_color(slint::Rgb8Pixel *pixel)
{
    std::swap(pixel->r, pixel->b);
}
// Already in panel byte order - nothing to do. Present so the templated flush path
// compiles unchanged; config.byte_swap should be false for this pixel type.
void byte_swap_buffer(slint::platform::Rgb565BigEndianPixel *, std::size_t) { }

void byte_swap_buffer(slint::platform::Rgb565Pixel *ptr, std::size_t len)
{
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

        ptr32[col]     = ((v0 & 0xFF00FF00) >> 8) | ((v0 & 0x00FF00FF) << 8);
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
void byte_swap_buffer(slint::Rgb8Pixel *ptr, std::size_t len)
{
    for (std::size_t i = 0; i < len; ++i) {
        std::swap(ptr[i].r, ptr[i].b);
    }
}
}

template<typename PixelType>
void EspPlatform<PixelType>::run_event_loop()
{
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
            } else {
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
                } else {
                    static uint32_t last_touch_poll = 0;
                    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
                    if (now_ms - last_touch_poll >= 20) {
                        last_touch_poll = now_ms;
                        read_touch = true;
                    }
                }
            }
            if (read_touch) {
                uint16_t touchpad_x[1] = { 0 };
                uint16_t touchpad_y[1] = { 0 };
                uint8_t touchpad_cnt = 0;

                /* Read touch controller data */
                esp_lcd_touch_read_data(touch_handle);

                /* Get coordinates */
                bool touchpad_pressed = esp_lcd_touch_get_coordinates(
                        touch_handle, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

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
                    m_window->window().dispatch_pointer_move_event(
                            slint::LogicalPosition({ last_touch_x, last_touch_y }));
                    if (!touch_down) {
                        m_window->window().dispatch_pointer_press_event(
                                slint::LogicalPosition({ last_touch_x, last_touch_y }),
                                slint::PointerEventButton::Left);
                    }
                    touch_down = true;
                } else if (touch_down) {
                    m_window->window().dispatch_pointer_release_event(
                            slint::LogicalPosition({ last_touch_x, last_touch_y }),
                            slint::PointerEventButton::Left);
                    m_window->window().dispatch_pointer_exit_event();
                    touch_down = false;
                }
            }

            if (std::exchange(m_window->needs_redraw, false)) {
                uint64_t t_start = esp_timer_get_time();
                uint64_t t_render = 0;
                uint64_t t_copy = 0;
                g_drawcall_us = 0;
                uint64_t t_wait_transmit = 0;
                uint64_t t_prepare = 0;
                uint32_t frame_lines = 0;

                extern SemaphoreHandle_t trans_sem;
                static bool dma_active = false;
                
                extern uint16_t *slint_chunk_buffer[2];
                extern int slint_chunk_lines; // resolved at init in display.cpp
                
                int idx = 0;
                int lines_in_chunk = 0;
                std::size_t chunk_start_y = 0;
                std::size_t chunk_start_x = 0;
                std::size_t chunk_end_x = 0;
                bool first_transfer = true;
                extern SemaphoreHandle_t trans_sem;

                // Slint runs prepare_scene (item tree walk, layout, text measurement,
                // dirty region computation) before it invokes the first line callback,
                // so the gap to that first call isolates it from the per-line loop.
                uint64_t t_first_line = 0;
                uint32_t line_callbacks = 0;

                auto region = m_window->m_renderer.render_by_line<PixelType>(
                        [&](std::size_t line_y, std::size_t line_start,
                            std::size_t line_end, auto &&render_fn) {
                            if (t_first_line == 0) {
                                t_first_line = esp_timer_get_time();
                            }
                            line_callbacks++;

                            std::size_t aligned_start = line_start & ~1;
                            std::size_t aligned_end = (line_end + 1) & ~1;
                            
                            bool flush = false;
                            if (lines_in_chunk > 0) {
                                if (line_y != chunk_start_y + lines_in_chunk || 
                                    aligned_start != chunk_start_x || 
                                    aligned_end != chunk_end_x || 
                                    lines_in_chunk == slint_chunk_lines) {
                                    flush = true;
                                }
                            }

                            if (flush) {
                                // Swap before taking trans_sem: the in-flight transfer is reading the
                                // other chunk buffer, so this overlaps the swap with it rather than
                                // leaving the bus idle for its duration.
                                uint64_t chunk_copy_start = esp_timer_get_time();
                                if (byte_swap) {
                                    std::size_t chunk_width = chunk_end_x - chunk_start_x;
                                    byte_swap_buffer(reinterpret_cast<PixelType*>(slint_chunk_buffer[idx]), chunk_width * lines_in_chunk);
                                }
                                t_copy += esp_timer_get_time() - chunk_copy_start;

                                uint64_t wait_start = esp_timer_get_time();
                                if (!first_transfer && trans_sem) {
                                    xSemaphoreTake(trans_sem, portMAX_DELAY);
                                }
                                t_wait_transmit += esp_timer_get_time() - wait_start;

                                timed_draw_bitmap(panel_handle, chunk_start_x, chunk_start_y,
                                                          chunk_end_x, chunk_start_y + lines_in_chunk, slint_chunk_buffer[idx]);
                                dma_active = true;
                                idx = (idx + 1) % 2;
                                lines_in_chunk = 0;
                                first_transfer = false;
                            }

                            if (lines_in_chunk == 0) {
                                chunk_start_y = line_y;
                                chunk_start_x = aligned_start;
                                chunk_end_x = aligned_end;
                            }

                            std::size_t chunk_width = chunk_end_x - chunk_start_x;
                            std::size_t span_offset = line_start - chunk_start_x;
                            std::size_t span_len = line_end - line_start;
                            PixelType *row = reinterpret_cast<PixelType *>(slint_chunk_buffer[idx])
                                    + lines_in_chunk * chunk_width;
                            std::span<PixelType> view { row + span_offset, span_len };

                            // Deliberately untimed. Timing each line cost two
                            // esp_timer_get_time() calls per scanline - 932 per full-screen
                            // frame - which is milliseconds of measurement overhead in the
                            // frame it is measuring. t_render is derived below instead.
                            render_fn(view);

                            // The panel needs even x bounds, so a dirty span with an odd edge leaves
                            // up to one untouched pixel on each side of this row. Replicate the
                            // neighbouring rendered pixel into it: a 1px colour bleed at the dirty
                            // edge is invisible, where clearing to black left a seam - and this
                            // replaces a full slint_chunk_lines-row memset per chunk.
                            if (span_len > 0) {
                                if (span_offset > 0) {
                                    row[span_offset - 1] = row[span_offset];
                                }
                                std::size_t span_end = span_offset + span_len;
                                if (span_end < chunk_width) {
                                    row[span_end] = row[span_end - 1];
                                }
                            }

                            lines_in_chunk++;
                        });
                
                if (lines_in_chunk > 0) {
                    // Swap before taking trans_sem: the in-flight transfer is reading the
                    // other chunk buffer, so this overlaps the swap with it rather than
                    // leaving the bus idle for its duration.
                    uint64_t chunk_copy_start = esp_timer_get_time();
                    if (byte_swap) {
                        std::size_t chunk_width = chunk_end_x - chunk_start_x;
                        byte_swap_buffer(reinterpret_cast<PixelType*>(slint_chunk_buffer[idx]), chunk_width * lines_in_chunk);
                    }
                    t_copy += esp_timer_get_time() - chunk_copy_start;

                    uint64_t wait_start = esp_timer_get_time();
                    if (!first_transfer && trans_sem) {
                        xSemaphoreTake(trans_sem, portMAX_DELAY);
                    }
                    t_wait_transmit += esp_timer_get_time() - wait_start;

                    timed_draw_bitmap(panel_handle, chunk_start_x, chunk_start_y,
                                              chunk_end_x, chunk_start_y + lines_in_chunk, slint_chunk_buffer[idx]);
                    dma_active = true;
                    first_transfer = false;
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

                uint64_t wait_start = esp_timer_get_time();
                if (dma_active && trans_sem) {
                    xSemaphoreTake(trans_sem, portMAX_DELAY);
                    dma_active = false;
                }
                t_wait_transmit += esp_timer_get_time() - wait_start;

#if SLINT_DIRTY_LOG
                // How much area Slint actually asked us to repaint, which is the input
                // to frame cost: rect count drives panel transactions, area drives both
                // rasterisation and bytes on the wire.
                {
                    static int dirty_frames = 0;
                    static uint32_t accum_rects = 0;
                    static uint32_t accum_area = 0;
                    static uint32_t max_area = 0;

                    uint32_t rects = 0, area = 0;
                    for (auto [o, s] : region.rectangles()) {
                        rects++;
                        area += (uint32_t)s.width * (uint32_t)s.height;
                    }
                    accum_rects += rects;
                    accum_area += area;
                    if (area > max_area) {
                        max_area = area;
                    }

                    if (++dirty_frames >= 60) {
                        uint32_t screen = (uint32_t)size.width * (uint32_t)size.height;
                        ESP_LOGI(TAG,
                                 "Dirty [60f]: rects=%.1f avg area=%lu px (%.1f%%), peak %lu px (%.1f%%)",
                                 accum_rects / 60.0f, (unsigned long)(accum_area / 60),
                                 100.0f * (float)(accum_area / 60) / (float)screen,
                                 (unsigned long)max_area, 100.0f * (float)max_area / (float)screen);
                        dirty_frames = 0;
                        accum_rects = 0;
                        accum_area = 0;
                        max_area = 0;
                    }
                }
#else
                (void)region;
#endif

                // How much of the panel Slint asked to be repainted this frame. Cheap to
                // total up, and it is the number that says whether invalidation or pixel
                // cost is the limit.
                uint32_t last_dirty_px = 0;
                for (auto [o, s] : region.rectangles()) {
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
                static int frame_count = 0;

                accum_render += t_render;
                accum_copy += t_copy;
                g_drawcall_accum += g_drawcall_us;
                g_shape_accum += g_shape_us;
                g_shape_us = 0;
                g_text_accum += g_text_us;
                g_text_us = 0;
                g_visited_accum += g_visited;
                g_drawn_accum += g_drawn;
                g_visited = 0;
                g_drawn = 0;
                if (g_phase_us[4] > g_phase_us[0]) {
                    g_phase_accum[0] += g_phase_us[1] - g_phase_us[0];
                    g_phase_accum[1] += g_phase_us[2] - g_phase_us[1];
                    g_phase_accum[2] += g_phase_us[4] - g_phase_us[3];
                }
                accum_wait_transmit += t_wait_transmit;
                accum_total += t_total;
                accum_prepare += t_prepare;
                accum_lines += frame_lines;
                frame_count++;

                if (frame_count >= 60) {
                    // "other" is what remains once scene prep and the measured flush work
                    // are removed: per-line loop overhead outside render_fn.
                    long long avg_total = accum_total / 60;
                    long long avg_prepare = accum_prepare / 60;
                    long long avg_other = avg_total - avg_prepare - (long long)(accum_render / 60) -
                                          (long long)(accum_copy / 60) - (long long)(accum_wait_transmit / 60);
                    ESP_LOGI(TAG,
                             "Slint Timing [60f avg]: prepare=%lld us, render=%lld us, copy=%lld us, "
                             "wait_transmit=%lld us, other=%lld us, total_draw=%lld us, lines=%lu, "
                             "drawcall=%lld us over %lu calls, setup=%lld walk=%lld scene=%lld, shape=%lld us x%lu, "
                             "text=%lld us x%lu, visited=%lu drawn=%lu",
                             avg_prepare,
                             accum_render / 60,
                             accum_copy / 60,
                             accum_wait_transmit / 60,
                             avg_other,
                             avg_total,
                             (unsigned long)(accum_lines / 60),
                             (long long)(g_drawcall_accum / 60),
                             (unsigned long)(g_drawcall_count / 60),
                             (long long)(g_phase_accum[0] / 60),
                             (long long)(g_phase_accum[1] / 60),
                             (long long)(g_phase_accum[2] / 60),
                             (long long)(g_shape_accum / 60),
                             (unsigned long)(g_shape_count / 60),
                             (long long)(g_text_accum / 60),
                             (unsigned long)(g_text_count / 60),
                             (unsigned long)(g_visited_accum / 60),
                             (unsigned long)(g_drawn_accum / 60));
                    accum_render = 0;
                    accum_copy = 0;
                    accum_wait_transmit = 0;
                    accum_total = 0;
                    accum_prepare = 0;
                    accum_lines = 0;
                    g_drawcall_accum = 0;
                    g_drawcall_count = 0;
                    g_phase_accum[0] = g_phase_accum[1] = g_phase_accum[2] = g_phase_accum[3] = 0;
                    g_shape_accum = 0;
                    g_shape_count = 0;
                    g_text_accum = 0;
                    g_text_count = 0;
                    g_visited_accum = 0;
                    g_drawn_accum = 0;
                    frame_count = 0;
                }
#else
                (void)t_prepare;
                (void)frame_lines;
                (void)t_render;
                (void)t_copy;
                (void)t_wait_transmit;
#endif
            }

            if (m_window->window().has_active_animations()) {
#ifdef TARGET_FPS
                uint32_t end_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                uint32_t elapsed = end_time - start_time;
                uint32_t target_time = 1000 / TARGET_FPS;
                if (elapsed < target_time) {
                    vTaskDelay(pdMS_TO_TICKS(target_time - elapsed));
                } else {
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

template<typename PixelType>
void EspPlatform<PixelType>::quit_event_loop()
{
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
    } else {
        xTaskNotifyGive(task);
    }
}

template<typename PixelType>
void EspPlatform<PixelType>::run_in_event_loop(slint::platform::Platform::Task event)
{
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
    } else {
        xTaskNotifyGive(task);
    }
}

template<typename PixelType>
TaskHandle_t EspPlatform<PixelType>::task = {};

void slint_esp_init(const SlintPlatformConfiguration<slint::platform::Rgb565Pixel> &config)
{
    slint::platform::set_platform(
            std::make_unique<EspPlatform<slint::platform::Rgb565Pixel>>(config));
}

void slint_esp_init(const SlintPlatformConfiguration<slint::platform::Rgb565BigEndianPixel> &config)
{
    slint::platform::set_platform(
            std::make_unique<EspPlatform<slint::platform::Rgb565BigEndianPixel>>(config));
}

void slint_esp_init(const SlintPlatformConfiguration<slint::Rgb8Pixel> &config)
{
    slint::platform::set_platform(std::make_unique<EspPlatform<slint::Rgb8Pixel>>(config));
}

void slint_esp_set_rotation(slint::platform::SoftwareRenderer::RenderingRotation rotation)
{
    if (EspPlatform<slint::platform::Rgb565Pixel>::active_platform && EspPlatform<slint::platform::Rgb565Pixel>::set_rotation_callback) {
        EspPlatform<slint::platform::Rgb565Pixel>::set_rotation_callback(EspPlatform<slint::platform::Rgb565Pixel>::active_platform, rotation);
    }
    else if (EspPlatform<slint::Rgb8Pixel>::active_platform && EspPlatform<slint::Rgb8Pixel>::set_rotation_callback) {
        EspPlatform<slint::Rgb8Pixel>::set_rotation_callback(EspPlatform<slint::Rgb8Pixel>::active_platform, rotation);
    }
}

// Force recompile for deferred byte swap

