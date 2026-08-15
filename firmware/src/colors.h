#ifndef _COLORS_H
#define _COLORS_H

// Structure
#define COLOR_BACKGROUND 0x000000
#define COLOR_PRIMARY 0x2095F6
#define COLOR_PRIMARY_TEXT 0xFFFFFF
#define COLOR_STRUCTURE 0x414141

#define RSSI_BAR_ON 0xFFFFFF
#define RSSI_BAR_OFF 0x717171

// Status colors
#define COLOR_CAUTION 0xFFC145  // LEVEL 1
#define COLOR_WARNING 0xFF8B3E  // LEVEL 2
#define COLOR_CRITICAL 0xFF6369 // LEVEL 3
#define COLOR_ACTIVE 0x676767   // Color structure lightened 20%

// LED status colors. The status colors above are tuned for the display, where a lifted green
// and blue channel reads as a soft tint. An RGB LED is additive, so the same lift reads as
// white mixed into the hue - COLOR_CRITICAL is 255,99,105, which is red plus about 40% white.
// The LED gets a saturated triad instead.
#define LED_COLOR_CAUTION 0xFFD000  // LEVEL 1
#define LED_COLOR_WARNING 0xFF5000  // LEVEL 2
#define LED_COLOR_CRITICAL 0xFF0000 // LEVEL 3
#define COLOR_SUCCESS 0x0EAD69
#endif