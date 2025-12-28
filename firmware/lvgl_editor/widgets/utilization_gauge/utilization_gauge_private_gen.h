/**
 * @file utilization_gauge_private_gen.h
 *
 */

#ifndef UTILIZATION_GAUGE_PRIVATE_H
#define UTILIZATION_GAUGE_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl_private.h"
#include "utilization_gauge.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_arc_t obj;  /* Base widget to extend */
} utilization_gauge_t;

extern const lv_obj_class_t utilization_gauge_class;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

#if LV_USE_XML
    void utilization_gauge_register(void);
#endif

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UTILIZATION_GAUGE_PRIVATE_H*/