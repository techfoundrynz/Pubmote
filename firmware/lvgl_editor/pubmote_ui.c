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

void pubmote_ui_init(const char * asset_path)
{
    pubmote_ui_init_gen(asset_path);
    LV_LOG_ERROR("Pubmote UI initialized");

    /* Add your own custom code here if needed */
    // Force linker to include custom code
    stats_screen_custom_init();
    settings_screen_custom_init();
}

/**********************
 *   STATIC FUNCTIONS
 **********************/