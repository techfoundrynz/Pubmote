/**
 * @file joystick_visual_private_gen.h
 *
 */

#ifndef JOYSTICK_VISUAL_PRIVATE_H
#define JOYSTICK_VISUAL_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl_private.h"
#include "joystick_visual.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_obj_t obj;  /* Base widget to extend */
    int32_t size;
    int32_t x;
    int32_t y;
    int32_t deadzone;
    bool active;
    lv_coord_t thickness;
    lv_obj_t * lv_obj_0;
    lv_obj_t * lv_obj_1;
    lv_obj_t * lv_obj_2;
    lv_obj_t * lv_obj_3;
    lv_obj_t * lv_obj_4;
    lv_obj_t * lv_obj_5;
    lv_obj_t * lv_obj_6;
} joystick_visual_t;

extern const lv_obj_class_t joystick_visual_class;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

#if LV_USE_XML
    void joystick_visual_register(void);
#endif

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*JOYSTICK_VISUAL_PRIVATE_H*/