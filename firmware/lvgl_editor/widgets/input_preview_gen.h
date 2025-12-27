/**
 * @file input_preview_gen.h
 *
 */

#ifndef INPUT_PREVIEW_GEN_H
#define INPUT_PREVIEW_GEN_H

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
 * Create a input_preview object
 * @param parent pointer to an object, it will be the parent of the new input_preview
 * @return pointer to the created input_preview
 */
lv_obj_t * input_preview_create(lv_obj_t * parent);
/**
 * Widget size
 * @param obj   pointer to a input_preview
 * @param size  Widget size
 */
void input_preview_set_size(lv_obj_t * input_preview, int32_t size);

/**
 * x proportion
 * @param obj   pointer to a input_preview
 * @param x  x proportion
 */
void input_preview_set_x(lv_obj_t * input_preview, int32_t x);

/**
 * y proportion
 * @param obj   pointer to a input_preview
 * @param y  y proportion
 */
void input_preview_set_y(lv_obj_t * input_preview, int32_t y);

/**
 * input_preview deadzone
 * @param obj   pointer to a input_preview
 * @param deadzone  deadzone
 */
void input_preview_set_deadzone(lv_obj_t * input_preview, int32_t deadzone);

/**
 * input_preview active
 * @param obj   pointer to a input_preview
 * @param active  active
 */
void input_preview_set_active(lv_obj_t * input_preview, bool active);

/**
 * input_preview thickness
 * @param obj   pointer to a input_preview
 * @param thickness  thickness
 */
void input_preview_set_thickness(lv_obj_t * input_preview, lv_coord_t thickness);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*INPUT_PREVIEW_GEN_H*/