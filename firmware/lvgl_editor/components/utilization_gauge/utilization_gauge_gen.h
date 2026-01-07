/**
 * @file utilization_gauge_gen.h
 */

#ifndef UTILIZATION_GAUGE_H
#define UTILIZATION_GAUGE_H

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

lv_obj_t * utilization_gauge_create(lv_obj_t * parent, lv_subject_t * bind_duty_cycle, lv_subject_t * bind_vehicle_state);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UTILIZATION_GAUGE_H*/