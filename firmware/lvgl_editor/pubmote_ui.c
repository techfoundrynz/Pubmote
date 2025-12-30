/**
 * @file pubmote_ui.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "pubmote_ui.h"

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

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

#include "custom/stats_screen/stats_screen.h"
#include "custom/settings_screen/settings_screen.h"

static void speed_format_observer(lv_observer_t * observer, lv_subject_t * subject) {
    LV_LOG_ERROR("CB");
    float speed = lv_subject_get_float(subject);
    // log type of speed
    LV_LOG_ERROR("Speed value: %f", speed);
    
    // Update format based on speed value
    if (speed < 10.0f) {
        lv_subject_copy_string(&state_speed_fmt, "%.1f");
    } else {
        lv_subject_copy_string(&state_speed_fmt, "%.0f");
    }
}

void pubmote_ui_init(const char * asset_path)
{
    pubmote_ui_init_gen(asset_path);
    LV_LOG_ERROR("Pubmote UI initialized");

    /* Add your own custom code here if needed */
    // Force linker to include custom code
    stats_screen_custom_init();
    settings_screen_custom_init();

    // Bind speed formatting subject to speed subject
    lv_subject_add_observer(&state_speed, speed_format_observer, NULL);
    speed_format_observer(NULL, &state_speed);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/