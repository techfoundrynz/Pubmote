#include "remote/color_utils.h"
#include <algorithm>
#include <cmath>

HSVColor rgb_to_hsv(uint32_t rgb) {
  float r = ((rgb >> 16) & 0xFF) / 255.0f;
  float g = ((rgb >> 8) & 0xFF) / 255.0f;
  float b = (rgb & 0xFF) / 255.0f;

  float cmax = std::max(r, std::max(g, b));
  float cmin = std::min(r, std::min(g, b));
  float diff = cmax - cmin;

  HSVColor out;
  if (diff == 0) {
    out.h = 0;
  }
  else if (cmax == r) {
    out.h = std::fmod(60.0f * ((g - b) / diff) + 360.0f, 360.0f);
  }
  else if (cmax == g) {
    out.h = std::fmod(60.0f * ((b - r) / diff) + 120.0f, 360.0f);
  }
  else if (cmax == b) {
    out.h = std::fmod(60.0f * ((r - g) / diff) + 240.0f, 360.0f);
  }

  if (cmax == 0) {
    out.s = 0;
  }
  else {
    out.s = (diff / cmax) * 100.0f;
  }

  out.v = cmax * 100.0f;
  return out;
}

uint32_t hsv_to_rgb(float h, float s, float v) {
  float c = (v / 100.0f) * (s / 100.0f);
  float h_prime = std::fmod(h / 60.0f, 6.0f);
  float x = c * (1.0f - std::abs(std::fmod(h_prime, 2.0f) - 1.0f));
  float m = (v / 100.0f) - c;

  float r = 0, g = 0, b = 0;
  if (h_prime >= 0 && h_prime < 1) {
    r = c;
    g = x;
    b = 0;
  }
  else if (h_prime >= 1 && h_prime < 2) {
    r = x;
    g = c;
    b = 0;
  }
  else if (h_prime >= 2 && h_prime < 3) {
    r = 0;
    g = c;
    b = x;
  }
  else if (h_prime >= 3 && h_prime < 4) {
    r = 0;
    g = x;
    b = c;
  }
  else if (h_prime >= 4 && h_prime < 5) {
    r = x;
    g = 0;
    b = c;
  }
  else if (h_prime >= 5 && h_prime < 6) {
    r = c;
    g = 0;
    b = x;
  }

  uint8_t R = (uint8_t)((r + m) * 255.0f);
  uint8_t G = (uint8_t)((g + m) * 255.0f);
  uint8_t B = (uint8_t)((b + m) * 255.0f);

  return (R << 16) | (G << 8) | B;
}

static slint::Rgba8Pixel hsv_to_rgb_pixel(float h_val, float s_val, float v_val) {
  float c = v_val * s_val;
  float h_prime = h_val / 60.0f;
  float x = c * (1.0f - std::abs(std::fmod(h_prime, 2.0f) - 1.0f));
  float m = v_val - c;

  float r = 0, g = 0, b = 0;
  if (0 <= h_prime && h_prime < 1) {
    r = c;
    g = x;
    b = 0;
  }
  else if (1 <= h_prime && h_prime < 2) {
    r = x;
    g = c;
    b = 0;
  }
  else if (2 <= h_prime && h_prime < 3) {
    r = 0;
    g = c;
    b = x;
  }
  else if (3 <= h_prime && h_prime < 4) {
    r = 0;
    g = x;
    b = c;
  }
  else if (4 <= h_prime && h_prime < 5) {
    r = x;
    g = 0;
    b = c;
  }
  else if (5 <= h_prime && h_prime <= 6) {
    r = c;
    g = 0;
    b = x;
  }

  return {(uint8_t)std::clamp((r + m) * 255.0f, 0.0f, 255.0f), (uint8_t)std::clamp((g + m) * 255.0f, 0.0f, 255.0f),
          (uint8_t)std::clamp((b + m) * 255.0f, 0.0f, 255.0f), 255};
}

slint::Image generate_color_slider_track(float w_len, float h_len, int mode, float hue, float sat, float lit) {
  int w = std::max(1, (int)std::ceil(w_len));
  int h = std::max(1, (int)std::ceil(h_len));

  slint::SharedPixelBuffer<slint::Rgba8Pixel> buffer(w, h);
  auto *data = buffer.begin();

  float rad = h / 2.0f;
  float cx1 = rad;
  float cx2 = w - rad;
  float cy = rad;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      // Rounded corners calculation
      bool outside = false;
      if (x < cx1) {
        float dx = x - cx1;
        float dy = y - cy;
        if (dx * dx + dy * dy > rad * rad)
          outside = true;
      }
      else if (x > cx2) {
        float dx = x - cx2;
        float dy = y - cy;
        if (dx * dx + dy * dy > rad * rad)
          outside = true;
      }

      if (outside) {
        data[y * w + x] = {0, 0, 0, 0}; // Transparent
      }
      else {
        float t = (float)x / (w > 1 ? (w - 1) : 1);
        slint::Rgba8Pixel color;
        if (mode == 0) {
          color = hsv_to_rgb_pixel(t * 360.0f, sat / 100.0f, lit / 100.0f);
        }
        else if (mode == 1) {
          color = hsv_to_rgb_pixel(hue, t, lit / 100.0f);
        }
        else {
          color = hsv_to_rgb_pixel(hue, sat / 100.0f, t);
        }
        data[y * w + x] = color;
      }
    }
  }

  return slint::Image(buffer);
}
