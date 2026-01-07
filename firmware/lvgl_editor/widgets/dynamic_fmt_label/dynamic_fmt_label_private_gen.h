/**
 * @file dynamic_fmt_label_private_gen.h
 *
 */

#ifndef DYNAMIC_FMT_LABEL_PRIVATE_H
#define DYNAMIC_FMT_LABEL_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl_private.h"
#include "dynamic_fmt_label.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_label_t obj;  /* Base widget to extend */
    lv_subject_t * bind_text;
    lv_subject_t * bind_text_fmt;
} dynamic_fmt_label_t;

extern const lv_obj_class_t dynamic_fmt_label_class;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

#if LV_USE_XML
    void dynamic_fmt_label_register(void);
#endif

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*DYNAMIC_FMT_LABEL_PRIVATE_H*/