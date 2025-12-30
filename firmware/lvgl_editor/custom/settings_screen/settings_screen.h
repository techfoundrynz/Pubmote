/**
 * @file settings_screen.h
 */

#ifndef SETTINGS_SCREEN_CUSTOM_H
#define SETTINGS_SCREEN_CUSTOM_H

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

  void settings_screen_custom_init(void);
  void settings_screen_load_start_cb(lv_event_t *e);
  void settings_screen_loaded_cb(lv_event_t *e);
  void settings_screen_unload_start_cb(lv_event_t *e);
  void settings_screen_unloaded_cb(lv_event_t *e);

  /**********************
   *      MACROS
   **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*SETTINGS_SCREEN_CUSTOM_H*/