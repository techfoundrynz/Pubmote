// Copyright © SixtyFPS GmbH <info@slint.dev>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Slint-Royalty-free-2.0 OR LicenseRef-Slint-Software-3.0

#include <deque>
#include <mutex>
#include "slint-esp.h"
#include "slint-platform.h"
#include "esp_lcd_panel_ops.h"
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
          buffer1(config.buffer1),
          buffer2(config.buffer2),
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
    std::optional<std::span<PixelType>> buffer1;
    std::optional<std::span<PixelType>> buffer2;
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

    auto buffer_type =
            buffer2 ? RepaintBufferType::SwappedBuffers : RepaintBufferType::ReusedBuffer;
    auto window = std::make_unique<EspWindowAdapter>(buffer_type, size);
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
}

template<typename PixelType>
void EspPlatform<PixelType>::run_event_loop()
{
    esp_lcd_panel_disp_on_off(panel_handle, true);

    TickType_t max_ticks_to_wait = portMAX_DELAY;

    if (touch_handle) {
        // Fall back to polling (every 30ms) to avoid CPU starvation from continuous interrupts
        // or interrupt storms on boards with floating touch interrupt pins.
        max_ticks_to_wait = pdMS_TO_TICKS(30);
    }
#if 0 // disabled for SPI panel compatibility
    if (buffer2) {
        sem_vsync_end = xSemaphoreCreateBinary();
        sem_gui_ready = xSemaphoreCreateBinary();
        esp_lcd_rgb_panel_event_callbacks_t cbs = {};
        cbs.on_vsync = on_vsync_event;
        esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, this);
    }
#endif

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

            static uint32_t last_touch_poll = 0;
            uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (touch_handle && (now_ms - last_touch_poll >= 20)) {
                last_touch_poll = now_ms;
                uint16_t touchpad_x[1] = { 0 };
                uint16_t touchpad_y[1] = { 0 };
                uint8_t touchpad_cnt = 0;

                /* Read touch controller data */
                esp_lcd_touch_read_data(touch_handle);

                /* Get coordinates */
                bool touchpad_pressed = esp_lcd_touch_get_coordinates(
                        touch_handle, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

                if (touchpad_pressed && touchpad_cnt > 0) {
                    auto scale_factor = m_window->window().scale_factor();
                    // ESP_LOGI(TAG, "x: %i, y: %i", touchpad_x[0], touchpad_y[0]);
                    last_touch_x = float(touchpad_x[0]) / scale_factor;
                    last_touch_y = float(touchpad_y[0]) / scale_factor;
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
                extern SemaphoreHandle_t trans_sem;
                
                if (buffer1) {
                    // Smart Bounding Box Chunking using PSRAM state
                    std::size_t dirty_min_x = size.width, dirty_max_x = 0;
                    std::size_t dirty_min_y = size.height, dirty_max_y = 0;
                    
                    m_window->m_renderer.render_by_line<PixelType>(
                        [&](std::size_t line_y, std::size_t line_start,
                            std::size_t line_end, auto &&render_fn) {
                            
                            std::span<PixelType> view { buffer1->data() + (line_y * size.width) + line_start, line_end - line_start };
                            render_fn(view);
                            
                            if (byte_swap) {
                                std::for_each(view.begin(), view.end(), [](auto &rgbpix) { byte_swap_color(&rgbpix); });
                            }
                            
                            dirty_min_x = std::min(dirty_min_x, line_start);
                            dirty_max_x = std::max(dirty_max_x, line_end);
                            dirty_min_y = std::min(dirty_min_y, line_y);
                            dirty_max_y = std::max(dirty_max_y, line_y + 1);
                        });
                        
                    if (dirty_max_x > dirty_min_x && dirty_max_y > dirty_min_y) {
                        std::size_t w = dirty_max_x - dirty_min_x;
                        static bool first_transfer = true;
                        extern const int slint_chunk_lines;
                        extern uint16_t *slint_chunk_buffer[2];
                        
                        for (std::size_t y = dirty_min_y; y < dirty_max_y; y += slint_chunk_lines) {
                            std::size_t h = std::min(dirty_max_y - y, (std::size_t)slint_chunk_lines);
                            
                            if (!first_transfer && trans_sem) {
                                xSemaphoreTake(trans_sem, portMAX_DELAY);
                            }
                            
                            // Pack the exact bounding box into the fast SRAM bounce buffer
                            for (std::size_t row = 0; row < h; row++) {
                                memcpy(slint_chunk_buffer[0] + row * w, buffer1->data() + (y + row) * size.width + dirty_min_x, w * sizeof(PixelType));
                            }
                            
                            esp_lcd_panel_draw_bitmap(panel_handle, dirty_min_x, y, dirty_max_x, y + h, slint_chunk_buffer[0]);
                            first_transfer = false;
                        }
                    }
                } else {
                    extern uint16_t *slint_chunk_buffer[2];
                    extern const int slint_chunk_lines;
                    
                    int idx = 0;
                    int lines_in_chunk = 0;
                    std::size_t chunk_start_y = 0;
                    std::size_t chunk_start_x = 0;
                    std::size_t chunk_end_x = 0;
                    bool first_transfer = true;
                    extern SemaphoreHandle_t trans_sem;

                    m_window->m_renderer.render_by_line<PixelType>(
                            [&](std::size_t line_y, std::size_t line_start,
                                std::size_t line_end, auto &&render_fn) {
                                
                                bool flush = false;
                                if (lines_in_chunk > 0) {
                                    if (line_y != chunk_start_y + lines_in_chunk || 
                                        line_start != chunk_start_x || 
                                        line_end != chunk_end_x || 
                                        lines_in_chunk == slint_chunk_lines) {
                                        flush = true;
                                    }
                                }

                                if (flush) {
                                    if (!first_transfer && trans_sem) {
                                        xSemaphoreTake(trans_sem, portMAX_DELAY);
                                    }
                                    esp_lcd_panel_draw_bitmap(panel_handle, chunk_start_x, chunk_start_y,
                                                              chunk_end_x, chunk_start_y + lines_in_chunk, slint_chunk_buffer[idx]);
                                    idx = (idx + 1) % 2;
                                    lines_in_chunk = 0;
                                    first_transfer = false;
                                }

                                if (lines_in_chunk == 0) {
                                    chunk_start_y = line_y;
                                    chunk_start_x = line_start;
                                    chunk_end_x = line_end;
                                }

                                std::size_t chunk_width = chunk_end_x - chunk_start_x;
                                std::span<PixelType> view { reinterpret_cast<PixelType*>(slint_chunk_buffer[idx]) + (lines_in_chunk * chunk_width), line_end - line_start };
                                render_fn(view);
                                
                                if (byte_swap) {
                                    std::for_each(view.begin(), view.end(),
                                                  [](auto &rgbpix) { byte_swap_color(&rgbpix); });
                                }
                                
                                lines_in_chunk++;
                            });
                    
                    if (lines_in_chunk > 0) {
                        if (!first_transfer && trans_sem) {
                            xSemaphoreTake(trans_sem, portMAX_DELAY);
                        }
                        esp_lcd_panel_draw_bitmap(panel_handle, chunk_start_x, chunk_start_y,
                                                  chunk_end_x, chunk_start_y + lines_in_chunk, slint_chunk_buffer[idx]);
                        first_transfer = false;
                    }

                    if (!first_transfer && trans_sem) {
                        xSemaphoreTake(trans_sem, portMAX_DELAY);
                    }
                }
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
        if (auto wait_time = slint::platform::duration_until_next_timer_update()) {
            ticks_to_wait = std::min(ticks_to_wait, pdMS_TO_TICKS(wait_time->count()));
        }
        // ESP_LOGI("SLINT-ESP-LOOP", "Waiting: touch=%p, max_ticks=%u, ticks_to_wait=%u",
        //          touch_handle, (unsigned int)max_ticks_to_wait, (unsigned int)ticks_to_wait);
        ulTaskNotifyTake(/*reset to zero*/ pdTRUE, ticks_to_wait);
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

void slint_esp_init(slint::PhysicalSize size, esp_lcd_panel_handle_t panel,
                    std::optional<esp_lcd_touch_handle_t> touch,
                    std::span<slint::platform::Rgb565Pixel> buffer1,
                    std::optional<std::span<slint::platform::Rgb565Pixel>> buffer2)
{

    SlintPlatformConfiguration<slint::platform::Rgb565Pixel> config {
        .size = size,
        .panel_handle = panel,
        .touch_handle = touch ? *touch : nullptr,
        .buffer1 = buffer1,
        .buffer2 = buffer2,
        // For compatibility with earlier versions of Slint, we compute the value of
        // byte_swap the way it was implemented in Slint (slint-esp) <= 1.6.0:
        .byte_swap = !buffer2.has_value()
    };
    slint_esp_init(config);
}

void slint_esp_init(const SlintPlatformConfiguration<slint::platform::Rgb565Pixel> &config)
{
    slint::platform::set_platform(
            std::make_unique<EspPlatform<slint::platform::Rgb565Pixel>>(config));
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
