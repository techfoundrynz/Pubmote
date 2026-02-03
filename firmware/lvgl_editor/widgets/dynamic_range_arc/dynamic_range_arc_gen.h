/**
 * @file dynamic_range_arc_gen.h
 *
 */

#ifndef DYNAMIC_RANGE_ARC_GEN_H
#define DYNAMIC_RANGE_ARC_GEN_H

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
 * Create a dynamic_range_arc object
 * @param parent pointer to an object, it will be the parent of the new dynamic_range_arc
 * @return pointer to the created dynamic_range_arc
 */
lv_obj_t * dynamic_range_arc_create(lv_obj_t * parent);
/**
 * dynamic_range_arc bind_value
 * @param obj   pointer to a dynamic_range_arc
 * @param bind_value  bind_value
 */
void dynamic_range_arc_bind_value(lv_obj_t * dynamic_range_arc, lv_subject_t * bind_value);

/**
 * dynamic_range_arc bind_max_value
 * @param obj   pointer to a dynamic_range_arc
 * @param bind_max_value  bind_max_value
 */
void dynamic_range_arc_bind_max_value(lv_obj_t * dynamic_range_arc, lv_subject_t * bind_max_value);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*DYNAMIC_RANGE_ARC_GEN_H*/