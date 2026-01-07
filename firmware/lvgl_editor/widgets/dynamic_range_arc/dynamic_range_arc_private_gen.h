/**
 * @file dynamic_range_arc_private_gen.h
 *
 */

#ifndef DYNAMIC_RANGE_ARC_PRIVATE_H
#define DYNAMIC_RANGE_ARC_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl_private.h"
#include "dynamic_range_arc.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_arc_t obj;  /* Base widget to extend */
    lv_subject_t * bind_value;
    lv_subject_t * bind_max_value;
} dynamic_range_arc_t;

extern const lv_obj_class_t dynamic_range_arc_class;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

#if LV_USE_XML
    void dynamic_range_arc_register(void);
#endif

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*DYNAMIC_RANGE_ARC_PRIVATE_H*/