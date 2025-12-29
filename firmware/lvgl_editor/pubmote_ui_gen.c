/**
 * @file pubmote_ui_gen.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "pubmote_ui_gen.h"

#if LV_USE_XML
#include "widgets/battery_gauge/battery_gauge_private_gen.h"
#include "widgets/dynamic_fmt_label/dynamic_fmt_label_private_gen.h"
#include "widgets/dynamic_range_arc/dynamic_range_arc_private_gen.h"
#include "widgets/input_preview/input_preview_private_gen.h"
#endif /* LV_USE_XML */

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/*----------------
 * Translations
 *----------------*/

/**********************
 *  GLOBAL VARIABLES
 **********************/

/*--------------------
 *  Permanent screens
 *-------------------*/

lv_obj_t * stats_screen = NULL;

/*----------------
 * Global styles
 *----------------*/

lv_style_t style_disabled;
lv_style_t style_reset;

/*----------------
 * Fonts
 *----------------*/

lv_font_t * inter_12;
extern lv_font_t inter_12_data;
lv_font_t * inter_14;
extern lv_font_t inter_14_data;
lv_font_t * inter_16;
extern lv_font_t inter_16_data;
lv_font_t * inter_24;
extern lv_font_t inter_24_data;
lv_font_t * inter_28;
extern lv_font_t inter_28_data;
lv_font_t * inter_42;
extern lv_font_t inter_42_data;
lv_font_t * inter_64;
extern lv_font_t inter_64_data;
lv_font_t * inter_72;
extern lv_font_t inter_72_data;
lv_font_t * inter_96;
extern lv_font_t inter_96_data;
lv_font_t * inter_bold_12;
extern lv_font_t inter_bold_12_data;
lv_font_t * inter_bold_14;
extern lv_font_t inter_bold_14_data;
lv_font_t * inter_bold_16;
extern lv_font_t inter_bold_16_data;
lv_font_t * inter_bold_24;
extern lv_font_t inter_bold_24_data;
lv_font_t * inter_bold_28;
extern lv_font_t inter_bold_28_data;
lv_font_t * inter_bold_42;
extern lv_font_t inter_bold_42_data;
lv_font_t * inter_bold_72;
extern lv_font_t inter_bold_72_data;
lv_font_t * inter_bold_96;
extern lv_font_t inter_bold_96_data;

/*----------------
 * Images
 *----------------*/

const void * splash;

/*----------------
 * Subjects
 *----------------*/

lv_subject_t state_battery_percent;
lv_subject_t state_battery_status;
lv_subject_t state_speed;
lv_subject_t state_speed_fmt;
lv_subject_t state_max_speed;
lv_subject_t state_duty_cycle;
lv_subject_t state_vehicle_state;
lv_subject_t state_motor_temp;
lv_subject_t state_cont_temp;
lv_subject_t state_trip_distance;
lv_subject_t state_adc1_active;
lv_subject_t state_adc2_active;
lv_subject_t settings_disp_brightness;
lv_subject_t settings_dark_text;
lv_subject_t settings_speed_label;
lv_subject_t settings_temp_label;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void pubmote_ui_init_gen(const char * asset_path)
{
    char buf[256];

    /*----------------
     * Global styles
     *----------------*/

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&style_disabled);
        lv_style_set_opa_layered(&style_disabled, (255 * 60 / 100));

        lv_style_init(&style_reset);
        lv_style_set_width(&style_reset, LV_SIZE_CONTENT);
        lv_style_set_height(&style_reset, LV_SIZE_CONTENT);
        lv_style_set_bg_opa(&style_reset, 0);
        lv_style_set_border_width(&style_reset, 0);
        lv_style_set_radius(&style_reset, 0);
        lv_style_set_pad_all(&style_reset, 0);

        style_inited = true;
    }

    /*----------------
     * Fonts
     *----------------*/

    /* get font 'inter_12' from a C array */
    inter_12 = &inter_12_data;
    /* get font 'inter_14' from a C array */
    inter_14 = &inter_14_data;
    /* get font 'inter_16' from a C array */
    inter_16 = &inter_16_data;
    /* get font 'inter_24' from a C array */
    inter_24 = &inter_24_data;
    /* get font 'inter_28' from a C array */
    inter_28 = &inter_28_data;
    /* get font 'inter_42' from a C array */
    inter_42 = &inter_42_data;
    /* get font 'inter_64' from a C array */
    inter_64 = &inter_64_data;
    /* get font 'inter_72' from a C array */
    inter_72 = &inter_72_data;
    /* get font 'inter_96' from a C array */
    inter_96 = &inter_96_data;
    /* get font 'inter_bold_12' from a C array */
    inter_bold_12 = &inter_bold_12_data;
    /* get font 'inter_bold_14' from a C array */
    inter_bold_14 = &inter_bold_14_data;
    /* get font 'inter_bold_16' from a C array */
    inter_bold_16 = &inter_bold_16_data;
    /* get font 'inter_bold_24' from a C array */
    inter_bold_24 = &inter_bold_24_data;
    /* get font 'inter_bold_28' from a C array */
    inter_bold_28 = &inter_bold_28_data;
    /* get font 'inter_bold_42' from a C array */
    inter_bold_42 = &inter_bold_42_data;
    /* get font 'inter_bold_72' from a C array */
    inter_bold_72 = &inter_bold_72_data;
    /* get font 'inter_bold_96' from a C array */
    inter_bold_96 = &inter_bold_96_data;


    /*----------------
     * Images
     *----------------*/
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/splash.png");
    splash = lv_strdup(buf);

    /*----------------
     * Subjects
     *----------------*/
    lv_subject_init_int(&state_battery_percent, 0);
    lv_subject_init_int(&state_battery_status, 0);
    lv_subject_init_float(&state_speed, 0);
    static char state_speed_fmt_buf[UI_SUBJECT_STRING_LENGTH];
    static char state_speed_fmt_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&state_speed_fmt,
                           state_speed_fmt_buf,
                           state_speed_fmt_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "%.1f"
                          );
    lv_subject_init_float(&state_max_speed, 40);
    lv_subject_init_int(&state_duty_cycle, 0);
    lv_subject_init_int(&state_vehicle_state, 0);
    lv_subject_init_int(&state_motor_temp, 0);
    lv_subject_init_int(&state_cont_temp, 0);
    lv_subject_init_float(&state_trip_distance, 0);
    lv_subject_init_int(&state_adc1_active, 0);
    lv_subject_init_int(&state_adc2_active, 0);
    lv_subject_init_int(&settings_disp_brightness, 100);
    lv_subject_init_int(&settings_dark_text, 0);
    static char settings_speed_label_buf[UI_SUBJECT_STRING_LENGTH];
    static char settings_speed_label_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&settings_speed_label,
                           settings_speed_label_buf,
                           settings_speed_label_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "KPH"
                          );
    static char settings_temp_label_buf[UI_SUBJECT_STRING_LENGTH];
    static char settings_temp_label_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&settings_temp_label,
                           settings_temp_label_buf,
                           settings_temp_label_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "C"
                          );

    /*----------------
     * Translations
     *----------------*/

#if LV_USE_XML
    /* Register widgets */
    battery_gauge_register();
    dynamic_fmt_label_register();
    dynamic_range_arc_register();
    input_preview_register();

    /* Register fonts */
    lv_xml_register_font(NULL, "inter_12", inter_12);
    lv_xml_register_font(NULL, "inter_14", inter_14);
    lv_xml_register_font(NULL, "inter_16", inter_16);
    lv_xml_register_font(NULL, "inter_24", inter_24);
    lv_xml_register_font(NULL, "inter_28", inter_28);
    lv_xml_register_font(NULL, "inter_42", inter_42);
    lv_xml_register_font(NULL, "inter_64", inter_64);
    lv_xml_register_font(NULL, "inter_72", inter_72);
    lv_xml_register_font(NULL, "inter_96", inter_96);
    lv_xml_register_font(NULL, "inter_bold_12", inter_bold_12);
    lv_xml_register_font(NULL, "inter_bold_14", inter_bold_14);
    lv_xml_register_font(NULL, "inter_bold_16", inter_bold_16);
    lv_xml_register_font(NULL, "inter_bold_24", inter_bold_24);
    lv_xml_register_font(NULL, "inter_bold_28", inter_bold_28);
    lv_xml_register_font(NULL, "inter_bold_42", inter_bold_42);
    lv_xml_register_font(NULL, "inter_bold_72", inter_bold_72);
    lv_xml_register_font(NULL, "inter_bold_96", inter_bold_96);

    /* Register subjects */
    lv_xml_register_subject(NULL, "state_battery_percent", &state_battery_percent);
    lv_xml_register_subject(NULL, "state_battery_status", &state_battery_status);
    lv_xml_register_subject(NULL, "state_speed", &state_speed);
    lv_xml_register_subject(NULL, "state_speed_fmt", &state_speed_fmt);
    lv_xml_register_subject(NULL, "state_max_speed", &state_max_speed);
    lv_xml_register_subject(NULL, "state_duty_cycle", &state_duty_cycle);
    lv_xml_register_subject(NULL, "state_vehicle_state", &state_vehicle_state);
    lv_xml_register_subject(NULL, "state_motor_temp", &state_motor_temp);
    lv_xml_register_subject(NULL, "state_cont_temp", &state_cont_temp);
    lv_xml_register_subject(NULL, "state_trip_distance", &state_trip_distance);
    lv_xml_register_subject(NULL, "state_adc1_active", &state_adc1_active);
    lv_xml_register_subject(NULL, "state_adc2_active", &state_adc2_active);
    lv_xml_register_subject(NULL, "settings_disp_brightness", &settings_disp_brightness);
    lv_xml_register_subject(NULL, "settings_dark_text", &settings_dark_text);
    lv_xml_register_subject(NULL, "settings_speed_label", &settings_speed_label);
    lv_xml_register_subject(NULL, "settings_temp_label", &settings_temp_label);

    /* Register callbacks */
    lv_xml_register_event_cb(NULL, "about_screen_load_start_cb", about_screen_load_start_cb);
    lv_xml_register_event_cb(NULL, "about_screen_loaded_cb", about_screen_loaded_cb);
    lv_xml_register_event_cb(NULL, "about_screen_unload_start_cb", about_screen_unload_start_cb);
    lv_xml_register_event_cb(NULL, "about_screen_unloaded_cb", about_screen_unloaded_cb);
    lv_xml_register_event_cb(NULL, "calibration_screen_load_start_cb", calibration_screen_load_start_cb);
    lv_xml_register_event_cb(NULL, "calibration_screen_loaded_cb", calibration_screen_loaded_cb);
    lv_xml_register_event_cb(NULL, "calibration_screen_unload_start_cb", calibration_screen_unload_start_cb);
    lv_xml_register_event_cb(NULL, "calibration_screen_unloaded_cb", calibration_screen_unloaded_cb);
    lv_xml_register_event_cb(NULL, "charge_screen_load_start_cb", charge_screen_load_start_cb);
    lv_xml_register_event_cb(NULL, "charge_screen_loaded_cb", charge_screen_loaded_cb);
    lv_xml_register_event_cb(NULL, "charge_screen_unload_start_cb", charge_screen_unload_start_cb);
    lv_xml_register_event_cb(NULL, "charge_screen_unloaded_cb", charge_screen_unloaded_cb);
    lv_xml_register_event_cb(NULL, "menu_screen_load_start_cb", menu_screen_load_start_cb);
    lv_xml_register_event_cb(NULL, "menu_screen_loaded_cb", menu_screen_loaded_cb);
    lv_xml_register_event_cb(NULL, "menu_screen_unload_start_cb", menu_screen_unload_start_cb);
    lv_xml_register_event_cb(NULL, "menu_screen_unloaded_cb", menu_screen_unloaded_cb);
    lv_xml_register_event_cb(NULL, "pairing_screen_load_start_cb", pairing_screen_load_start_cb);
    lv_xml_register_event_cb(NULL, "pairing_screen_loaded_cb", pairing_screen_loaded_cb);
    lv_xml_register_event_cb(NULL, "pairing_screen_unload_start_cb", pairing_screen_unload_start_cb);
    lv_xml_register_event_cb(NULL, "pairing_screen_unloaded_cb", pairing_screen_unloaded_cb);
    lv_xml_register_event_cb(NULL, "settings_screen_load_start_cb", settings_screen_load_start_cb);
    lv_xml_register_event_cb(NULL, "settings_screen_loaded_cb", settings_screen_loaded_cb);
    lv_xml_register_event_cb(NULL, "settings_screen_unload_start_cb", settings_screen_unload_start_cb);
    lv_xml_register_event_cb(NULL, "settings_screen_unloaded_cb", settings_screen_unloaded_cb);
    lv_xml_register_event_cb(NULL, "stats_screen_load_start_cb", stats_screen_load_start_cb);
    lv_xml_register_event_cb(NULL, "stats_screen_loaded_cb", stats_screen_loaded_cb);
    lv_xml_register_event_cb(NULL, "stats_screen_unload_start_cb", stats_screen_unload_start_cb);
    lv_xml_register_event_cb(NULL, "stats_screen_unloaded_cb", stats_screen_unloaded_cb);
    lv_xml_register_event_cb(NULL, "stats_screen_gesture_cb", stats_screen_gesture_cb);
    lv_xml_register_event_cb(NULL, "update_screen_load_start_cb", update_screen_load_start_cb);
    lv_xml_register_event_cb(NULL, "update_screen_loaded_cb", update_screen_loaded_cb);
    lv_xml_register_event_cb(NULL, "update_screen_unload_start_cb", update_screen_unload_start_cb);
    lv_xml_register_event_cb(NULL, "update_screen_unloaded_cb", update_screen_unloaded_cb);
#endif

    /* Register all the global assets so that they won't be created again when globals.xml is parsed.
     * While running in the editor skip this step to update the preview when the XML changes */
#if LV_USE_XML && !defined(LV_EDITOR_PREVIEW)
    /* Register images */
    lv_xml_register_image(NULL, "splash", splash);
#endif

#if LV_USE_XML == 0
    /*--------------------
     *  Permanent screens
     *-------------------*/
    /* If XML is enabled it's assumed that the permanent screens are created
     * manaully from XML using lv_xml_create() */
    /* To allow screens to reference each other, create them all before calling the sceen create functions */
    stats_screen = lv_obj_create(NULL);

    stats_screen_create();
#endif
}

/* Callbacks */
#if defined(LV_EDITOR_PREVIEW)
void __attribute__((weak)) about_screen_load_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("about_screen_load_start_cb was called\n");
}
void __attribute__((weak)) about_screen_loaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("about_screen_loaded_cb was called\n");
}
void __attribute__((weak)) about_screen_unload_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("about_screen_unload_start_cb was called\n");
}
void __attribute__((weak)) about_screen_unloaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("about_screen_unloaded_cb was called\n");
}
void __attribute__((weak)) calibration_screen_load_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("calibration_screen_load_start_cb was called\n");
}
void __attribute__((weak)) calibration_screen_loaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("calibration_screen_loaded_cb was called\n");
}
void __attribute__((weak)) calibration_screen_unload_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("calibration_screen_unload_start_cb was called\n");
}
void __attribute__((weak)) calibration_screen_unloaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("calibration_screen_unloaded_cb was called\n");
}
void __attribute__((weak)) charge_screen_load_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("charge_screen_load_start_cb was called\n");
}
void __attribute__((weak)) charge_screen_loaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("charge_screen_loaded_cb was called\n");
}
void __attribute__((weak)) charge_screen_unload_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("charge_screen_unload_start_cb was called\n");
}
void __attribute__((weak)) charge_screen_unloaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("charge_screen_unloaded_cb was called\n");
}
void __attribute__((weak)) menu_screen_load_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("menu_screen_load_start_cb was called\n");
}
void __attribute__((weak)) menu_screen_loaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("menu_screen_loaded_cb was called\n");
}
void __attribute__((weak)) menu_screen_unload_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("menu_screen_unload_start_cb was called\n");
}
void __attribute__((weak)) menu_screen_unloaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("menu_screen_unloaded_cb was called\n");
}
void __attribute__((weak)) pairing_screen_load_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("pairing_screen_load_start_cb was called\n");
}
void __attribute__((weak)) pairing_screen_loaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("pairing_screen_loaded_cb was called\n");
}
void __attribute__((weak)) pairing_screen_unload_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("pairing_screen_unload_start_cb was called\n");
}
void __attribute__((weak)) pairing_screen_unloaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("pairing_screen_unloaded_cb was called\n");
}
void __attribute__((weak)) settings_screen_load_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("settings_screen_load_start_cb was called\n");
}
void __attribute__((weak)) settings_screen_loaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("settings_screen_loaded_cb was called\n");
}
void __attribute__((weak)) settings_screen_unload_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("settings_screen_unload_start_cb was called\n");
}
void __attribute__((weak)) settings_screen_unloaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("settings_screen_unloaded_cb was called\n");
}
void __attribute__((weak)) stats_screen_load_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("stats_screen_load_start_cb was called\n");
}
void __attribute__((weak)) stats_screen_loaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("stats_screen_loaded_cb was called\n");
}
void __attribute__((weak)) stats_screen_unload_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("stats_screen_unload_start_cb was called\n");
}
void __attribute__((weak)) stats_screen_unloaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("stats_screen_unloaded_cb was called\n");
}
void __attribute__((weak)) stats_screen_gesture_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("stats_screen_gesture_cb was called\n");
}
void __attribute__((weak)) update_screen_load_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("update_screen_load_start_cb was called\n");
}
void __attribute__((weak)) update_screen_loaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("update_screen_loaded_cb was called\n");
}
void __attribute__((weak)) update_screen_unload_start_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("update_screen_unload_start_cb was called\n");
}
void __attribute__((weak)) update_screen_unloaded_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG("update_screen_unloaded_cb was called\n");
}
#endif

/**********************
 *   STATIC FUNCTIONS
 **********************/