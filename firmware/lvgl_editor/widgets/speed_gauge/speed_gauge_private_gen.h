/**
 * @file speed_gauge_private_gen.h
 *
 */

#ifndef SPEED_GAUGE_PRIVATE_H
#define SPEED_GAUGE_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl_private.h"
#include "speed_gauge.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_arc_t obj;  /* Base widget to extend */
    lv_subject_t * bind_speed;
    lv_subject_t * bind_max_speed;
} speed_gauge_t;

extern const lv_obj_class_t speed_gauge_class;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

#if LV_USE_XML
    void speed_gauge_register(void);
#endif

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*SPEED_GAUGE_PRIVATE_H*/