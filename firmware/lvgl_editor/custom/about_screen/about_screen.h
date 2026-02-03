/**
 * @file about_screen.h
 */

#ifndef ABOUT_SCREEN_CUSTOM_H
#define ABOUT_SCREEN_CUSTOM_H

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

  void about_screen_custom_init(void);
  void about_screen_load_start_cb(lv_event_t *e);
  void about_screen_loaded_cb(lv_event_t *e);
  void about_screen_unload_start_cb(lv_event_t *e);
  void about_screen_unloaded_cb(lv_event_t *e);

  /**********************
   *      MACROS
   **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*ABOUT_SCREEN_CUSTOM_H*/