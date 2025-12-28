/**
 * @file input_preview_private_gen.h
 *
 */

#ifndef INPUT_PREVIEW_PRIVATE_H
#define INPUT_PREVIEW_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl_private.h"
#include "input_preview.h"

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
} input_preview_t;

extern const lv_obj_class_t input_preview_class;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

#if LV_USE_XML
    void input_preview_register(void);
#endif

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*INPUT_PREVIEW_PRIVATE_H*/