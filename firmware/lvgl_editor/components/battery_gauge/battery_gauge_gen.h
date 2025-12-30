/**
 * @file battery_gauge_gen.h
 */

#ifndef BATTERY_GAUGE_H
#define BATTERY_GAUGE_H

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

lv_obj_t * battery_gauge_create(lv_obj_t * parent, lv_coord_t width, lv_coord_t height, lv_subject_t * bind_percent, lv_subject_t * bind_charge_state);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*BATTERY_GAUGE_H*/