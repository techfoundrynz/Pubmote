/**
 * @file color_picker_private_gen.h
 *
 */

#ifndef COLOR_PICKER_PRIVATE_H
#define COLOR_PICKER_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl_private.h"
#include "color_picker.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_slider_t obj;  /* Base widget to extend */
    int32_t size;
    lv_subject_t * bind_value;
} color_picker_t;

extern const lv_obj_class_t color_picker_class;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

#if LV_USE_XML
    void color_picker_register(void);
#endif

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*COLOR_PICKER_PRIVATE_H*/