// Copyright © SixtyFPS GmbH <info@slint.dev>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Slint-Royalty-free-2.0 OR LicenseRef-Slint-Software-3.0

#pragma once

#include "esp_lcd_touch.h"
#include "esp_lcd_types.h"
#include "slint-platform.h"

// One staging buffer per dirty rectangle. Matches the renderer's DirtyRegion::MAX_COUNT; a
// smaller value still renders correctly but evicts chunks before they fill.
#ifndef SLINT_CHUNK_ACCUMULATORS
  #define SLINT_CHUNK_ACCUMULATORS 3
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
  /// Called on the event-loop task when an active touch is released.
  void (*touch_release_callback)() = nullptr;
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

/**
 * Direct overlay: pixels pushed straight to the panel after Slint's own frame, for
 * content the software renderer is slow to draw (such as a game's warp layer).
 * The callback runs on the UI task with the panel bus held, after Slint's chunks are on
 * the panel. `repainted` is true when Slint drew anything that frame, which may have
 * covered the overlay's area; the overlay then redraws what it owns. Nothing else may be
 * drawn by Slint inside the overlay's area while it is set. Pass nullptr to remove it.
 */
using slint_esp_overlay_fn = void (*)(bool repainted, void *user);
void slint_esp_set_overlay(slint_esp_overlay_fn fn, void *user);
/// Asks for an overlay pass on this or the next loop iteration, even if Slint has
/// nothing to redraw. Call from the UI task (e.g. a Slint timer).
void slint_esp_request_overlay();
/// Only inside the overlay callback. Draws a w x h block of big-endian RGB565 pixels,
/// given in logical (unrotated) coordinates with row pitch `stride`, at logical (x, y).
/// x, y, w and h must be even: the panel only accepts even windows. Returns once the
/// panel has read the pixels, so the caller may reuse them.
bool slint_esp_overlay_draw(int x, int y, int w, int h, const uint16_t *pixels, int stride);

extern volatile uint32_t slint_esp_frame_counter;
extern volatile uint32_t slint_esp_last_frame_us;
