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

#define THEME_SUCCESS lv_color_hex(0x0EAD69)

#define THEME_WARN lv_color_hex(0xFECC0B)

#define THEME_DANGER lv_color_hex(0xFF8B3E)

#define THEME_CRITICAL lv_color_hex(0xFF6369)

#define THEME_TEXT lv_color_hex(0xFFFFFF)

#define THEME_BG lv_color_hex(0x000000)

#define THEME_STRUCTURE1 lv_color_hex(0x000000)

#define THEME_STRUCTURE2 lv_color_hex(0x242424)

#define THEME_STRUCTURE3 lv_color_hex(0x5a5a5a)

#define THEME_STRUCTURE4 lv_color_hex(0x9d9d9d)

#define THEME_STRUCTURE5 lv_color_hex(0xffffff)

#define SETTINGS_THEME_COLOR lv_color_hex(0xFF46A2)

#define VEHICLE_STATE_NORMAL 0

#define VEHICLE_STATE_WARN 1

#define VEHICLE_STATE_DANGER 2

#define VEHICLE_STATE_CRITICAL 3

#define THEME_DEFAULT_BUTTON_FONT "inter_bold_16"

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
extern const void * charge;

/*----------------
 * Subjects
 *----------------*/

extern lv_subject_t state_connection_state;
extern lv_subject_t state_connection_state_label;
extern lv_subject_t state_battery_percent;
extern lv_subject_t state_battery_status;
extern lv_subject_t state_speed;
extern lv_subject_t state_speed_fmt;
extern lv_subject_t state_max_speed;
extern lv_subject_t state_duty_cycle;
extern lv_subject_t state_vehicle_state;
extern lv_subject_t state_motor_temp;
extern lv_subject_t state_cont_temp;
extern lv_subject_t state_trip_distance;
extern lv_subject_t state_adc1_active;
extern lv_subject_t state_adc2_active;
extern lv_subject_t state_pocket_mode;
extern lv_subject_t state_factory_reset;
extern lv_subject_t state_has_wifi_creds;
extern lv_subject_t state_update_step;
extern lv_subject_t settings_dark_text;
extern lv_subject_t settings_speed_label;
extern lv_subject_t settings_temp_label;
extern lv_subject_t settings_color_h;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/*----------------
 * Event Callbacks
 *----------------*/

void about_screen_load_start_cb(lv_event_t * e);
void about_screen_loaded_cb(lv_event_t * e);
void about_screen_unload_start_cb(lv_event_t * e);
void about_screen_unloaded_cb(lv_event_t * e);
void calibration_screen_load_start_cb(lv_event_t * e);
void calibration_screen_loaded_cb(lv_event_t * e);
void calibration_screen_unload_start_cb(lv_event_t * e);
void calibration_screen_unloaded_cb(lv_event_t * e);
void charge_screen_load_start_cb(lv_event_t * e);
void charge_screen_loaded_cb(lv_event_t * e);
void charge_screen_unload_start_cb(lv_event_t * e);
void charge_screen_unloaded_cb(lv_event_t * e);
void menu_screen_load_start_cb(lv_event_t * e);
void menu_screen_loaded_cb(lv_event_t * e);
void menu_screen_unload_start_cb(lv_event_t * e);
void menu_screen_unloaded_cb(lv_event_t * e);
void factory_reset(lv_event_t * e);
void pairing_screen_load_start_cb(lv_event_t * e);
void pairing_screen_loaded_cb(lv_event_t * e);
void pairing_screen_unload_start_cb(lv_event_t * e);
void pairing_screen_unloaded_cb(lv_event_t * e);
void settings_screen_load_start_cb(lv_event_t * e);
void settings_screen_loaded_cb(lv_event_t * e);
void settings_screen_unload_start_cb(lv_event_t * e);
void settings_screen_unloaded_cb(lv_event_t * e);
void settings_screen_brightness_slider_change_cb(lv_event_t * e);
void settings_screen_double_press_action_dropdown_change_cb(lv_event_t * e);
void settings_screen_screen_rotation_dropdown_change_cb(lv_event_t * e);
void settings_screen_shutdown_time_dropdown_change_cb(lv_event_t * e);
void settings_screen_temp_units_dropdown_change_cb(lv_event_t * e);
void settings_screen_distance_units_dropdown_change_cb(lv_event_t * e);
void settings_screen_startup_sound_dropdown_change_cb(lv_event_t * e);
void settings_screen_cancel_cb(lv_event_t * e);
void settings_screen_save_cb(lv_event_t * e);
void stats_screen_load_start_cb(lv_event_t * e);
void stats_screen_loaded_cb(lv_event_t * e);
void stats_screen_unload_start_cb(lv_event_t * e);
void stats_screen_unloaded_cb(lv_event_t * e);
void update_screen_load_start_cb(lv_event_t * e);
void update_screen_loaded_cb(lv_event_t * e);
void update_screen_unload_start_cb(lv_event_t * e);
void update_screen_unloaded_cb(lv_event_t * e);

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
#include "widgets/color_picker/color_picker_gen.h"
#include "widgets/dynamic_fmt_label/dynamic_fmt_label_gen.h"
#include "widgets/dynamic_range_arc/dynamic_range_arc_gen.h"
#include "components/bar/bar_gen.h"
#include "components/battery_gauge/battery_gauge_gen.h"
#include "components/button/button_gen.h"
#include "components/button/button_outlined_gen.h"
#include "components/button/button_primary_gen.h"
#include "components/div/div_gen.h"
#include "components/dropdown/dropdown_gen.h"
#include "components/footpad_indicator/footpad_indicator_gen.h"
#include "components/horizontal_pager/horizontal_page_gen.h"
#include "components/horizontal_pager/horizontal_pager_gen.h"
#include "components/input_visualization/input_visualization_gen.h"
#include "components/page/body_gen.h"
#include "components/page/footer_gen.h"
#include "components/page/footer_buttons_gen.h"
#include "components/page/header_gen.h"
#include "components/page/page_gen.h"
#include "components/roller/roller_gen.h"
#include "components/slider/slider_gen.h"
#include "components/speed_gauge/speed_gauge_gen.h"
#include "components/switch/switch_gen.h"
#include "components/text/h1_gen.h"
#include "components/text/label_gen.h"
#include "components/utilization_gauge/utilization_gauge_gen.h"
#include "screens/about/about_screen_gen.h"
#include "screens/calibration/calibration_screen_gen.h"
#include "screens/charge/charge_screen_gen.h"
#include "screens/menu/menu_screen_gen.h"
#include "screens/pairing/pairing_screen_gen.h"
#include "screens/settings/settings_screen_gen.h"
#include "screens/splash/splash_screen_gen.h"
#include "screens/stats/stats_screen_gen.h"
#include "screens/update/update_screen_gen.h"

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*PUBMOTE_UI_GEN_H*/