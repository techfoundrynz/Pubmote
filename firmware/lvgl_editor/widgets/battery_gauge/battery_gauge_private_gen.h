/**
 * @file battery_gauge_private_gen.h
 *
 */

#ifndef BATTERY_GAUGE_PRIVATE_H
#define BATTERY_GAUGE_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl_private.h"
#include "battery_gauge.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_obj_t obj;  /* Base widget to extend */
    lv_coord_t width;
    lv_coord_t height;
    int32_t fill;
    lv_obj_t * lv_obj_0;
    lv_obj_t * lv_obj_1;
    lv_obj_t * lv_obj_2;
} battery_gauge_t;

extern const lv_obj_class_t battery_gauge_class;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

#if LV_USE_XML
    void battery_gauge_register(void);
#endif

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*BATTERY_GAUGE_PRIVATE_H*/