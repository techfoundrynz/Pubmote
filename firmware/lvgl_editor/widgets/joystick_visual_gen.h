/**
 * @file joystick_visual_gen.h
 *
 */

#ifndef JOYSTICK_VISUAL_GEN_H
#define JOYSTICK_VISUAL_GEN_H

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
 * Create a joystick_visual object
 * @param parent pointer to an object, it will be the parent of the new joystick_visual
 * @return pointer to the created joystick_visual
 */
lv_obj_t * joystick_visual_create(lv_obj_t * parent);
/**
 * Widget size
 * @param obj   pointer to a joystick_visual
 * @param size  Widget size
 */
void joystick_visual_set_size(lv_obj_t * joystick_visual, int32_t size);

/**
 * x proportion
 * @param obj   pointer to a joystick_visual
 * @param x  x proportion
 */
void joystick_visual_set_x(lv_obj_t * joystick_visual, int32_t x);

/**
 * y proportion
 * @param obj   pointer to a joystick_visual
 * @param y  y proportion
 */
void joystick_visual_set_y(lv_obj_t * joystick_visual, int32_t y);

/**
 * joystick_visual deadzone
 * @param obj   pointer to a joystick_visual
 * @param deadzone  deadzone
 */
void joystick_visual_set_deadzone(lv_obj_t * joystick_visual, int32_t deadzone);

/**
 * joystick_visual active
 * @param obj   pointer to a joystick_visual
 * @param active  active
 */
void joystick_visual_set_active(lv_obj_t * joystick_visual, bool active);

/**
 * joystick_visual thickness
 * @param obj   pointer to a joystick_visual
 * @param thickness  thickness
 */
void joystick_visual_set_thickness(lv_obj_t * joystick_visual, lv_coord_t thickness);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*JOYSTICK_VISUAL_GEN_H*/