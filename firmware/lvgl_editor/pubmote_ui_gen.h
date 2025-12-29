/**
 * @file pubmote_ui_gen.h
 */

#ifndef PUBMOTE_UI_GEN_H
#define PUBMOTE_UI_GEN_H

#ifndef UI_SUBJECT_STRING_LENGTH
#define UI_SUBJECT_STRING_LENGTH 256
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

/*********************
 *      DEFINES
 *********************/

#define UNIT_SM 6

#define UNIT_MD 12

#define UNIT_LG 18

#define UNIT_XL 24

#define OPA_MUTED lv_pct(20)

#define LIGHT lv_color_hex(0xffffff)

#define DARK lv_color_hex(0x0e0e0e)

#define SURFACE_PRIMARY_LIGHT lv_color_hex(0x0e0e0e)

#define TEXT_ON_SURFACE_PRIMARY_LIGHT lv_color_hex(0xffffff)

#define SURFACE_PRIMARY_DARK lv_color_hex(0xffffff)

#define TEXT_ON_SURFACE_PRIMARY_DARK lv_color_hex(0x0e0e0e)

#define BG_PRIMARY_LIGHT lv_color_hex(0xffffff)

#define BG_PRIMARY_DARK lv_color_hex(0x0e0e0e)

#define BG_SECONDARY_LIGHT lv_color_hex(0xf0f0f0)

#define BG_SECONDARY_DARK lv_color_hex(0x373130)

#define BG_TERTIARY_LIGHT lv_color_hex(0xf0f0f0)

#define BG_TERTIARY_DARK lv_color_hex(0x373130)

#define ACCENT1_LIGHT lv_color_hex(0xAF4ADE)

#define ACCENT1_DARK lv_color_hex(0xAF4ADE)

#define ACCENT1_50_LIGHT lv_color_hex(0xD2B1F6)

#define ACCENT1_50_DARK lv_color_hex(0x7E4CB7)

#define ACCENT2_LIGHT lv_color_hex(0xe9deaf)

#define ACCENT2_DARK lv_color_hex(0x887A3D)

#define ACCENT2_50_LIGHT lv_color_hex(0xf3f0e7)

#define ACCENT2_50_DARK lv_color_hex(0x4A473E)

#define THEME_SUCCESS lv_color_hex(0x0EAD69)

#define THEME_WARN lv_color_hex(0xFECC0B)

#define THEME_DANGER lv_color_hex(0xFF8B3E)

#define THEME_CRITICAL lv_color_hex(0xFF6369)

#define SETTINGS_THEME_COLOR lv_color_hex(0xFF46A2)

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL VARIABLES
 **********************/

/*-------------------
 * Permanent screens
 *------------------*/

extern lv_obj_t * stats_screen;

/*----------------
 * Global styles
 *----------------*/

extern lv_style_t style_disabled;
extern lv_style_t style_reset;

/*----------------
 * Fonts
 *----------------*/

extern lv_font_t * inter_12;

extern lv_font_t * inter_14;

extern lv_font_t * inter_16;

extern lv_font_t * inter_24;

extern lv_font_t * inter_28;

extern lv_font_t * inter_42;

extern lv_font_t * inter_64;

extern lv_font_t * inter_72;

extern lv_font_t * inter_96;

extern lv_font_t * inter_bold_12;

extern lv_font_t * inter_bold_14;

extern lv_font_t * inter_bold_16;

extern lv_font_t * inter_bold_24;

extern lv_font_t * inter_bold_28;

extern lv_font_t * inter_bold_42;

extern lv_font_t * inter_bold_72;

extern lv_font_t * inter_bold_96;

/*----------------
 * Images
 *----------------*/

extern const void * splash;

/*----------------
 * Subjects
 *----------------*/

extern lv_subject_t state_battery_percent;
extern lv_subject_t state_battery_status;
extern lv_subject_t state_speed;
extern lv_subject_t state_speed_fmt;
extern lv_subject_t state_max_speed;
extern lv_subject_t state_duty_cycle;
extern lv_subject_t state_motor_temp;
extern lv_subject_t state_cont_temp;
extern lv_subject_t state_trip_distance;
extern lv_subject_t state_adc1_active;
extern lv_subject_t state_adc2_active;
extern lv_subject_t settings_disp_brightness;
extern lv_subject_t settings_dark_text;
extern lv_subject_t settings_speed_label;
extern lv_subject_t settings_temp_label;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/*----------------
 * Event Callbacks
 *----------------*/

/**
 * Initialize the component library
 */

void pubmote_ui_init_gen(const char * asset_path);

/**********************
 *      MACROS
 **********************/

/**********************
 *   POST INCLUDES
 **********************/

/*Include all the widget and components of this library*/
#include "widgets/battery_gauge/battery_gauge_gen.h"
#include "widgets/dynamic_fmt_label/dynamic_fmt_label_gen.h"
#include "widgets/input_preview/input_preview_gen.h"
#include "widgets/speed_gauge/speed_gauge_gen.h"
#include "widgets/utilization_gauge/utilization_gauge_gen.h"
#include "components/bar/bar_gen.h"
#include "components/button/button_gen.h"
#include "components/button/button_outlined_gen.h"
#include "components/button/button_primary_gen.h"
#include "components/circle_button/circle_button_gen.h"
#include "components/div/div_gen.h"
#include "components/footpad_indicator/footpad_indicator_gen.h"
#include "components/icon_button/icon_button_gen.h"
#include "components/page/body_gen.h"
#include "components/page/footer_gen.h"
#include "components/page/footer_buttons_gen.h"
#include "components/page/header_gen.h"
#include "components/page/page_gen.h"
#include "components/slider/slider_gen.h"
#include "components/switch/switch_gen.h"
#include "components/text/h1_gen.h"
#include "components/text/label_gen.h"
#include "screens/about_screen_gen.h"
#include "screens/calibration_screen_gen.h"
#include "screens/charge_screen_gen.h"
#include "screens/menu_screen_gen.h"
#include "screens/pairing_screen_gen.h"
#include "screens/settings_screen_gen.h"
#include "screens/splash_screen_gen.h"
#include "screens/stats_screen_gen.h"
#include "screens/update_screen_gen.h"

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*PUBMOTE_UI_GEN_H*/