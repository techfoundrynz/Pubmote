/**
 * @file pubmote_ui_gen.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "pubmote_ui_gen.h"

#if LV_USE_XML
#include "widgets/battery_gauge/battery_gauge_private_gen.h"
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

/*----------------
 * Images
 *----------------*/

const void * icon_heart;
const void * icon_plus;
const void * icon_minus;
const void * icon_theme;
const void * splash;

/*----------------
 * Subjects
 *----------------*/

lv_subject_t dark_theme;
lv_subject_t theme_color;
lv_subject_t speaker;
lv_subject_t speaker_vol;
lv_subject_t settings_disp_brightness;

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


    /*----------------
     * Images
     *----------------*/
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/icon_heart.png");
    icon_heart = lv_strdup(buf);
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/icon_plus.png");
    icon_plus = lv_strdup(buf);
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/icon_minus.png");
    icon_minus = lv_strdup(buf);
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/icon_theme.png");
    icon_theme = lv_strdup(buf);
    lv_snprintf(buf, 256, "%s%s", asset_path, "images/splash.png");
    splash = lv_strdup(buf);

    /*----------------
     * Subjects
     *----------------*/
    lv_subject_init_int(&dark_theme, 0);
    static char theme_color_buf[UI_SUBJECT_STRING_LENGTH];
    static char theme_color_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&theme_color,
                           theme_color_buf,
                           theme_color_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "0xFFFFFF"
                          );
    lv_subject_init_int(&speaker, 1);
    lv_subject_init_int(&speaker_vol, 40);
    lv_subject_init_int(&settings_disp_brightness, 100);

    /*----------------
     * Translations
     *----------------*/

#if LV_USE_XML
    /* Register widgets */
    battery_gauge_register();
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
    lv_xml_register_font(NULL, "inter_bold_12", inter_bold_12);
    lv_xml_register_font(NULL, "inter_bold_14", inter_bold_14);
    lv_xml_register_font(NULL, "inter_bold_16", inter_bold_16);
    lv_xml_register_font(NULL, "inter_bold_24", inter_bold_24);
    lv_xml_register_font(NULL, "inter_bold_28", inter_bold_28);
    lv_xml_register_font(NULL, "inter_bold_42", inter_bold_42);
    lv_xml_register_font(NULL, "inter_bold_72", inter_bold_72);

    /* Register subjects */
    lv_xml_register_subject(NULL, "dark_theme", &dark_theme);
    lv_xml_register_subject(NULL, "theme_color", &theme_color);
    lv_xml_register_subject(NULL, "speaker", &speaker);
    lv_xml_register_subject(NULL, "speaker_vol", &speaker_vol);
    lv_xml_register_subject(NULL, "settings_disp_brightness", &settings_disp_brightness);

    /* Register callbacks */
#endif

    /* Register all the global assets so that they won't be created again when globals.xml is parsed.
     * While running in the editor skip this step to update the preview when the XML changes */
#if LV_USE_XML && !defined(LV_EDITOR_PREVIEW)
    /* Register images */
    lv_xml_register_image(NULL, "icon_heart", icon_heart);
    lv_xml_register_image(NULL, "icon_plus", icon_plus);
    lv_xml_register_image(NULL, "icon_minus", icon_minus);
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
    stats_screen = lv_obj_create(NULL);

    stats_screen_create();
#endif
}

/* Callbacks */

/**********************
 *   STATIC FUNCTIONS
 **********************/