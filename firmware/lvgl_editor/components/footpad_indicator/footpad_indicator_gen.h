/**
 * @file footpad_indicator_gen.h
 */

#ifndef FOOTPAD_INDICATOR_H
#define FOOTPAD_INDICATOR_H

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

lv_obj_t * footpad_indicator_create(lv_obj_t * parent, lv_subject_t * bind_left_active, lv_subject_t * bind_right_active);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*FOOTPAD_INDICATOR_H*/