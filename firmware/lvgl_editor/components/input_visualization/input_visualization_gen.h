/**
 * @file input_visualization_gen.h
 */

#ifndef INPUT_VISUALIZATION_H
#define INPUT_VISUALIZATION_H

#ifdef __cplusplus
extern "C" {
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

lv_obj_t * input_visualization_create(lv_obj_t * parent, int32_t x, int32_t y, int32_t deadzone, bool active);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*INPUT_VISUALIZATION_H*/