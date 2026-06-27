#pragma once

#include <slint.h>
#include <stdint.h>

struct HSVColor {
  float h; // 0..360
  float s; // 0..100
  float v; // 0..100
};

HSVColor rgb_to_hsv(uint32_t rgb);
uint32_t hsv_to_rgb(float h, float s, float v);

slint::Image generate_color_slider_track(float w_len, float h_len, int mode, float hue, float sat, float lit);
