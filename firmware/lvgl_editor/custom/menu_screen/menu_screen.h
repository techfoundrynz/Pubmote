/**
 * @file menu_screen.h
 */

#ifndef MENU_SCREEN_CUSTOM_H
#define MENU_SCREEN_CUSTOM_H

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

  void menu_screen_custom_init(void);
  void menu_screen_load_start_cb(lv_event_t *e);
  void menu_screen_loaded_cb(lv_event_t *e);
  void menu_screen_unload_start_cb(lv_event_t *e);
  void menu_screen_unloaded_cb(lv_event_t *e);
  void factory_reset(lv_event_t *e);

  /**********************
   *      MACROS
   **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*MENU_SCREEN_CUSTOM_H*/