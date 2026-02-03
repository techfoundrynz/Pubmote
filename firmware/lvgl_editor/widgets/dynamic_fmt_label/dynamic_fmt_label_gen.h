/**
 * @file dynamic_fmt_label_gen.h
 *
 */

#ifndef DYNAMIC_FMT_LABEL_GEN_H
#define DYNAMIC_FMT_LABEL_GEN_H

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
 * Create a dynamic_fmt_label object
 * @param parent pointer to an object, it will be the parent of the new dynamic_fmt_label
 * @return pointer to the created dynamic_fmt_label
 */
lv_obj_t * dynamic_fmt_label_create(lv_obj_t * parent);
/**
 * dynamic_fmt_label bind_text
 * @param obj   pointer to a dynamic_fmt_label
 * @param bind_text  bind_text
 */
void dynamic_fmt_label_bind_text(lv_obj_t * dynamic_fmt_label, lv_subject_t * bind_text);

/**
 * dynamic_fmt_label bind_text_fmt
 * @param obj   pointer to a dynamic_fmt_label
 * @param bind_text_fmt  bind_text_fmt
 */
void dynamic_fmt_label_bind_text_fmt(lv_obj_t * dynamic_fmt_label, lv_subject_t * bind_text_fmt);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*DYNAMIC_FMT_LABEL_GEN_H*/