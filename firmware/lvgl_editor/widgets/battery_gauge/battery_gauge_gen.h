/**
 * @file battery_gauge_gen.h
 *
 */

#ifndef BATTERY_GAUGE_GEN_H
#define BATTERY_GAUGE_GEN_H

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
 * Create a battery_gauge object
 * @param parent pointer to an object, it will be the parent of the new battery_gauge
 * @return pointer to the created battery_gauge
 */
lv_obj_t * battery_gauge_create(lv_obj_t * parent);
/**
 * Widget width
 * @param obj   pointer to a battery_gauge
 * @param width  Widget width
 */
void battery_gauge_set_width(lv_obj_t * battery_gauge, lv_coord_t width);

/**
 * Widget height
 * @param obj   pointer to a battery_gauge
 * @param height  Widget height
 */
void battery_gauge_set_height(lv_obj_t * battery_gauge, lv_coord_t height);

/**
 * Percentage fill
 * @param obj   pointer to a battery_gauge
 * @param fill  Percentage fill
 */
void battery_gauge_set_fill(lv_obj_t * battery_gauge, int32_t fill);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*BATTERY_GAUGE_GEN_H*/