/**
 * @file color_picker_gen.h
 *
 */

#ifndef COLOR_PICKER_GEN_H
#define COLOR_PICKER_GEN_H

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
 * Create a color_picker object
 * @param parent pointer to an object, it will be the parent of the new color_picker
 * @return pointer to the created color_picker
 */
lv_obj_t * color_picker_create(lv_obj_t * parent);
/**
 * Wheel diameter
 * @param obj   pointer to a color_picker
 * @param size  Wheel diameter
 */
void color_picker_set_size(lv_obj_t * color_picker, int32_t size);

/**
 * color_picker bind_value
 * @param obj   pointer to a color_picker
 * @param bind_value  bind_value
 */
void color_picker_bind_value(lv_obj_t * color_picker, lv_subject_t * bind_value);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*COLOR_PICKER_GEN_H*/