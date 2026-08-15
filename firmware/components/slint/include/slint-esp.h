// Copyright © SixtyFPS GmbH <info@slint.dev>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Slint-Royalty-free-2.0 OR LicenseRef-Slint-Software-3.0

#pragma once

#include "esp_lcd_touch.h"
#include "esp_lcd_types.h"
#include "slint-platform.h"

// One staging buffer per dirty rectangle. Matches the renderer's DirtyRegion::MAX_COUNT; a
// smaller value still renders correctly but evicts chunks before they fill.
#ifndef SLINT_CHUNK_ACCUMULATORS
#  define SLINT_CHUNK_ACCUMULATORS 3
#endif


/**
 * This data structure configures the Slint platform for use with ESP-IDF, in particular
 * the esp_lcd component (
 * https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/lcd.html )
 * for touch input and on-screen rendering.
 *
 * This copy renders line by line only: the scene is rendered into chunks of internal SRAM
 * (see slint_chunk_buffer in display.cpp) and each chunk is flushed to the panel as it is
 * produced. The upstream single- and double-buffered paths have been removed - a full frame
 * buffer would have to live in PSRAM on this hardware, and reading it back each frame costs
 * more than rendering into SRAM chunks.
 *
 *  The data structure is a template where the pixel type is configurable.
 *  The default depends on the sdkconfig, but you can use either `slint::Rgb8Pixel` or
 *  `slint::platform::Rgb565Pixel`, depending on how the display is configured.
 */
template <typename PixelType =
#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
              slint::Rgb8Pixel
#else
              slint::platform::Rgb565Pixel
#endif
          >

struct SlintPlatformConfiguration {
  /// The size of the screen in pixels.
  slint::PhysicalSize size;
  /// The handle to the display as previously initialized by `bsp_display_new` or
  /// `esp_lcd_panel_init`. Must be set to a valid, non-null esp_lcd_panel_handle_t.
  esp_lcd_panel_handle_t panel_handle = nullptr;
  /// The touch screen handle, if the device is equipped with a touch screen. Set to nullptr
  /// otherwise;
  esp_lcd_touch_handle_t touch_handle = nullptr;
  slint::platform::SoftwareRenderer::RenderingRotation rotation =
      slint::platform::SoftwareRenderer::RenderingRotation::NoRotation;
  /// Swap the 2 bytes of RGB 565 pixels before sending to the display, or turn 24-bit RGB into
  /// BGR. Use this if your CPU is little endian but the display expects big-endian.
  union {
    [[deprecated("Renamed to byte_swap")]] bool color_swap_16;
    bool byte_swap = false;
  };
  /// Note there is deliberately no draw-window alignment knob here, the equivalent of LVGL's
  /// rounder callback. These panels do need even window coordinates - the SH8601 README says
  /// so and CO5300 shares that driver - but rounding in the driver means inventing pixels the
  /// renderer never drew, and at the edge of a dirty band those are never repainted. The
  /// renderer rounds the dirty region instead, so what arrives here is already even and every
  /// pixel of it has genuinely been painted.
};

template <typename... Args> SlintPlatformConfiguration(Args...) -> SlintPlatformConfiguration<>;

/**
 * Initialize the Slint platform for ESP-IDF.
 *
 * This must be called before any other call to the Slint library.
 */
void slint_esp_init(const SlintPlatformConfiguration<slint::platform::Rgb565Pixel> &config);
void slint_esp_init(const SlintPlatformConfiguration<slint::Rgb8Pixel> &config);
void slint_esp_init(const SlintPlatformConfiguration<slint::platform::Rgb565BigEndianPixel> &config);
void slint_esp_set_rotation(slint::platform::SoftwareRenderer::RenderingRotation rotation);

extern volatile uint32_t slint_esp_frame_counter;
extern volatile uint32_t slint_esp_last_frame_us;
