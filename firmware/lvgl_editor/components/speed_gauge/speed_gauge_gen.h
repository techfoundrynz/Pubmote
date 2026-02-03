/**
 * @file speed_gauge_gen.h
 */

#ifndef SPEED_GAUGE_H
#define SPEED_GAUGE_H

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

lv_obj_t * speed_gauge_create(lv_obj_t * parent, lv_subject_t * bind_speed, lv_subject_t * bind_max_speed);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*SPEED_GAUGE_H*/