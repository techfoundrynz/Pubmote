/**
 * @file speed_gauge_gen.h
 *
 */

#ifndef SPEED_GAUGE_GEN_H
#define SPEED_GAUGE_GEN_H

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
 * Create a speed_gauge object
 * @param parent pointer to an object, it will be the parent of the new speed_gauge
 * @return pointer to the created speed_gauge
 */
lv_obj_t * speed_gauge_create(lv_obj_t * parent);
/**
 * speed_gauge bind_speed
 * @param obj   pointer to a speed_gauge
 * @param bind_speed  bind_speed
 */
void speed_gauge_bind_speed(lv_obj_t * speed_gauge, lv_subject_t * bind_speed);

/**
 * speed_gauge bind_max_speed
 * @param obj   pointer to a speed_gauge
 * @param bind_max_speed  bind_max_speed
 */
void speed_gauge_bind_max_speed(lv_obj_t * speed_gauge, lv_subject_t * bind_max_speed);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*SPEED_GAUGE_GEN_H*/