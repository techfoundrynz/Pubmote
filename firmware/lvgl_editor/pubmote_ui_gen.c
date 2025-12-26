/**
 * @file pubmote_ui_gen.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "pubmote_ui_gen.h"

#if LV_USE_XML
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

lv_obj_t * stats = NULL;

/*----------------
 * Global styles
 *----------------*/

lv_style_t style_disabled;
lv_style_t style_reset;
lv_style_t figma_import_test;

/*----------------
 * Fonts
 *----------------*/

lv_font_t * geist_semibold_12;
extern lv_font_t geist_semibold_12_data;
lv_font_t * geist_semibold_14;
extern lv_font_t geist_semibold_14_data;
lv_font_t * geist_bold_16;
extern lv_font_t geist_bold_16_data;
lv_font_t * geist_semibold_20;
extern lv_font_t geist_semibold_20_data;
lv_font_t * geist_semibold_28;
extern lv_font_t geist_semibold_28_data;
lv_font_t * geist_regular_12;
extern lv_font_t geist_regular_12_data;
lv_font_t * geist_regular_14;
extern lv_font_t geist_regular_14_data;
lv_font_t * geist_light_60;
extern lv_font_t geist_light_60_data;
lv_font_t * literata_80;
extern lv_font_t literata_80_data;
lv_font_t * abril_fatface_80;
lv_font_t * big_shoulders_80;

/*----------------
 * Images
 *----------------*/

const void * light_temp_arc_bg;
const void * icon_heart;
const void * icon_volume_max;
const void * icon_volume_min;
const void * icon_volume_none;
const void * song_cover_1;
const void * icon_pin;
const void * icon_theme;
const void * splash;

/*----------------
 * Subjects
 *----------------*/

lv_subject_t dark_theme;
lv_subject_t thermostat_on;
lv_subject_t thermostat_temp;
lv_subject_t room_temp;
lv_subject_t alarm_on;
lv_subject_t alarm_hour;
lv_subject_t alarm_min;
lv_subject_t speaker;
lv_subject_t speaker_vol;
lv_subject_t light_temperature;
lv_subject_t light_temperature_temp;

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

        lv_style_init(&figma_import_test);
        lv_style_set_width(&figma_import_test, 24);
        lv_style_set_height(&figma_import_test, 24);

        style_inited = true;
    }

    /*----------------
     * Fonts
     *----------------*/

    /* get font 'geist_semibold_12' from a C array */
    geist_semibold_12 = &geist_semibold_12_data;
    /* get font 'geist_semibold_14' from a C array */
    geist_semibold_14 = &geist_semibold_14_data;
    /* get font 'geist_bold_16' from a C array */
    geist_bold_16 = &geist_bold_16_data;
    /* get font 'geist_semibold_20' from a C array */
    geist_semibold_20 = &geist_semibold_20_data;
    /* get font 'geist_semibold_28' from a C array */
    geist_semibold_28 = &geist_semibold_28_data;
    /* get font 'geist_regular_12' from a C array */
    geist_regular_12 = &geist_regular_12_data;
    /* get font 'geist_regular_14' from a C array */
    geist_regular_14 = &geist_regular_14_data;
    /* get font 'geist_light_60' from a C array */
    geist_light_60 = &geist_light_60_data;
    /* get font 'literata_80' from a C array */
    literata_80 = &literata_80_data;
    /* create bin font 'abril_fatface_80' from file */
    lv_snprintf(buf, 256, "%s%s", asset_path, "fonts/AbrilFatface/abril_fatface_80");
    abril_fatface_80 = lv_binfont_create(buf);
    /* create bin font 'big_shoulders_80' from file */
    lv_snprintf(buf, 256, "%s%s", asset_path, "fonts/BigShoulders/big_shoulders_80");
    big_shoulders_80 = lv_binfont_create(buf);


    /*----------------
     * Images
     *----------------*/
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/light_temp_arc_bg.png");
    light_temp_arc_bg = lv_strdup(buf);
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/icon_heart.png");
    icon_heart = lv_strdup(buf);
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/icon_volume_max.png");
    icon_volume_max = lv_strdup(buf);
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/icon_volume_min.png");
    icon_volume_min = lv_strdup(buf);
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/icon_volume_none.png");
    icon_volume_none = lv_strdup(buf);
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/song_cover_1.png");
    song_cover_1 = lv_strdup(buf);
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/icon_pin.png");
    icon_pin = lv_strdup(buf);
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/icon_theme.png");
    icon_theme = lv_strdup(buf);
    lv_snprintf(buf, 256, "%s%s", asset_path, "../assets/splash.png");
    splash = lv_strdup(buf);

    /*----------------
     * Subjects
     *----------------*/
    lv_subject_init_int(&dark_theme, 0);
    lv_subject_init_int(&thermostat_on, 1);
    lv_subject_init_int(&thermostat_temp, 16);
    lv_subject_init_int(&room_temp, 24);
    lv_subject_init_int(&alarm_on, 1);
    lv_subject_init_int(&alarm_hour, 06);
    lv_subject_init_int(&alarm_min, 36);
    lv_subject_init_int(&speaker, 1);
    lv_subject_init_int(&speaker_vol, 40);
    lv_subject_init_int(&light_temperature, 1);
    lv_subject_init_int(&light_temperature_temp, 3000);

    /*----------------
     * Translations
     *----------------*/

#if LV_USE_XML
    /* Register widgets */

    /* Register fonts */
    lv_xml_register_font(NULL, "geist_semibold_12", geist_semibold_12);
    lv_xml_register_font(NULL, "geist_semibold_14", geist_semibold_14);
    lv_xml_register_font(NULL, "geist_bold_16", geist_bold_16);
    lv_xml_register_font(NULL, "geist_semibold_20", geist_semibold_20);
    lv_xml_register_font(NULL, "geist_semibold_28", geist_semibold_28);
    lv_xml_register_font(NULL, "geist_regular_12", geist_regular_12);
    lv_xml_register_font(NULL, "geist_regular_14", geist_regular_14);
    lv_xml_register_font(NULL, "geist_light_60", geist_light_60);
    lv_xml_register_font(NULL, "literata_80", literata_80);
    lv_xml_register_font(NULL, "abril_fatface_80", abril_fatface_80);
    lv_xml_register_font(NULL, "big_shoulders_80", big_shoulders_80);

    /* Register subjects */
    lv_xml_register_subject(NULL, "dark_theme", &dark_theme);
    lv_xml_register_subject(NULL, "thermostat_on", &thermostat_on);
    lv_xml_register_subject(NULL, "thermostat_temp", &thermostat_temp);
    lv_xml_register_subject(NULL, "room_temp", &room_temp);
    lv_xml_register_subject(NULL, "alarm_on", &alarm_on);
    lv_xml_register_subject(NULL, "alarm_hour", &alarm_hour);
    lv_xml_register_subject(NULL, "alarm_min", &alarm_min);
    lv_xml_register_subject(NULL, "speaker", &speaker);
    lv_xml_register_subject(NULL, "speaker_vol", &speaker_vol);
    lv_xml_register_subject(NULL, "light_temperature", &light_temperature);
    lv_xml_register_subject(NULL, "light_temperature_temp", &light_temperature_temp);

    /* Register callbacks */
#endif

    /* Register all the global assets so that they won't be created again when globals.xml is parsed.
     * While running in the editor skip this step to update the preview when the XML changes */
#if LV_USE_XML && !defined(LV_EDITOR_PREVIEW)
    /* Register images */
    lv_xml_register_image(NULL, "light_temp_arc_bg", light_temp_arc_bg);
    lv_xml_register_image(NULL, "icon_heart", icon_heart);
    lv_xml_register_image(NULL, "icon_volume_max", icon_volume_max);
    lv_xml_register_image(NULL, "icon_volume_min", icon_volume_min);
    lv_xml_register_image(NULL, "icon_volume_none", icon_volume_none);
    lv_xml_register_image(NULL, "song_cover_1", song_cover_1);
    lv_xml_register_image(NULL, "icon_pin", icon_pin);
    lv_xml_register_image(NULL, "icon_theme", icon_theme);
    lv_xml_register_image(NULL, "splash", splash);
#endif

#if LV_USE_XML == 0
    /*--------------------
     *  Permanent screens
     *-------------------*/
    /* If XML is enabled it's assumed that the permanent screens are created
     * manaully from XML using lv_xml_create() */
    /* To allow screens to reference each other, create them all before calling the sceen create functions */
    stats = lv_obj_create(NULL);

    stats_create();
#endif
}

/* Callbacks */

/**********************
 *   STATIC FUNCTIONS
 **********************/