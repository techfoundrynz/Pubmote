/**
 * @file stats_screen.h
 */

#ifndef STATS_SCREEN_CUSTOM_H
#define STATS_SCREEN_CUSTOM_H

#ifdef __cplusplus
extern "C"
{
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

  /**********************
   *      TYPEDEFS
   **********************/

  /**********************
   * GLOBAL PROTOTYPES
   **********************/

  void stats_screen_custom_init(void);
  void stats_screen_gesture_cb(lv_event_t *e);
  void stats_screen_load_start_cb(lv_event_t *e);
  void stats_screen_loaded_cb(lv_event_t *e);
  void stats_screen_unload_start_cb(lv_event_t *e);
  void stats_screen_unloaded_cb(lv_event_t *e);

  /**********************
   *      MACROS
   **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*STATS_SCREEN_CUSTOM_H*/