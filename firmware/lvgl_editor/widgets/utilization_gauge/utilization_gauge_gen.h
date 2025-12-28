/**
 * @file utilization_gauge_gen.h
 *
 */

#ifndef UTILIZATION_GAUGE_GEN_H
#define UTILIZATION_GAUGE_GEN_H

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

#include "pubmote_ui_gen.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a utilization_gauge object
 * @param parent pointer to an object, it will be the parent of the new utilization_gauge
 * @return pointer to the created utilization_gauge
 */
lv_obj_t * utilization_gauge_create(lv_obj_t * parent);
/**
 * utilization_gauge bind_duty_cycle
 * @param obj   pointer to a utilization_gauge
 * @param bind_duty_cycle  bind_duty_cycle
 */
void utilization_gauge_bind_duty_cycle(lv_obj_t * utilization_gauge, lv_subject_t * bind_duty_cycle);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UTILIZATION_GAUGE_GEN_H*/