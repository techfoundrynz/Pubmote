#include "stats_screen.h"
#include "../../pubmote_ui.h"
#include "../../screens/menu/menu_screen_gen.h"

void stats_screen_custom_init(void) {
  // This function exists to force the linker to include this file
}

void stats_screen_gesture_cb(lv_event_t *e) {
  // Handle gesture events here
  LV_LOG_USER("Stats screen gesture event");
  if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_BOTTOM) {
    // Ensure the global stats_screen variable is set for the "Back" button in menu code
    if (stats_screen == NULL) {
      stats_screen = lv_event_get_current_target(e);
    }

    lv_indev_wait_release(lv_indev_active());
    lv_obj_t *menu_scr = menu_screen_create();
    if (menu_scr) {
      lv_screen_load_anim(menu_scr, LV_SCREEN_LOAD_ANIM_OVER_BOTTOM, 200, 0, false);
      LV_LOG_USER("DOWN GESTURE DETECTED - Loading Menu");
    }
    else {
      LV_LOG_ERROR("Failed to create menu screen");
    }
  }
}

void stats_screen_load_start_cb(lv_event_t *e) {
  // Handle screen load start events here
}

void stats_screen_loaded_cb(lv_event_t *e) {
  // Handle screen loaded events here
}

void stats_screen_unload_start_cb(lv_event_t *e) {
  // Handle screen unload start events here
}

void stats_screen_unloaded_cb(lv_event_t *e) {
  // Handle screen unloaded events here
}