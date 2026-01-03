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

#include "custom/about_screen/about_screen.h"
#include "custom/menu_screen/menu_screen.h"
#include "custom/pairing_screen/pairing_screen.h"
#include "custom/settings_screen/settings_screen.h"
#include "custom/stats_screen/stats_screen.h"
#include "custom/update_screen/update_screen.h"
#include "custom/calibration_screen/calibration_screen.h"

static void speed_format_observer(lv_observer_t *observer, lv_subject_t *subject) {
  float speed = lv_subject_get_float(subject);

  // Update format based on speed value
  if (speed < 10.0f) {
    lv_subject_copy_string(&state_speed_fmt, "%.1f");
  }
  else {
    lv_subject_copy_string(&state_speed_fmt, "%.0f");
  }
}

static void connection_state_observer(lv_observer_t *observer, lv_subject_t *subject) {
  int32_t state = lv_subject_get_int(subject);

  switch (state) {
  case 0:
    lv_subject_copy_string(&state_connection_state_label, "Disconnected");
    break;
  case 1:
    lv_subject_copy_string(&state_connection_state_label, "Connected");
    break;
  case 2:
    lv_subject_copy_string(&state_connection_state_label, "Reconnecting");
    break;
  default:
    break;
  }
}

void pubmote_ui_init(const char *asset_path) {
  pubmote_ui_init_gen(asset_path);
  LV_LOG_USER("Pubmote UI initialized");

  /* Add your own custom code here if needed */
  // Force linker to include custom code
  stats_screen_custom_init();
  menu_screen_custom_init();
  about_screen_custom_init();
  pairing_screen_custom_init();
  update_screen_custom_init();
  settings_screen_custom_init();
  calibration_screen_custom_init();

  // Bind speed formatting subject to speed subject
  lv_subject_add_observer(&state_speed, speed_format_observer, NULL);
  speed_format_observer(NULL, &state_speed);
  // Bind connection state label subject to connection state subject
  lv_subject_add_observer(&state_connection_state, connection_state_observer, NULL);
  connection_state_observer(NULL, &state_connection_state);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/