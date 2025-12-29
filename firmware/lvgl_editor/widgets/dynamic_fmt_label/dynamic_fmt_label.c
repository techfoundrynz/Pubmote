/**
 * @file dynamic_fmt_label.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "dynamic_fmt_label_private_gen.h"
#include "pubmote_ui.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void update_value(dynamic_fmt_label_t * dynamic_fmt_label);
static void value_observer_cb(lv_observer_t * obs, lv_subject_t * subject);
static void fmt_observer_cb(lv_observer_t * obs, lv_subject_t * subject);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void dynamic_fmt_label_constructor_hook(lv_obj_t * obj)
{

}

void dynamic_fmt_label_destructor_hook(lv_obj_t * obj)
{

}

void dynamic_fmt_label_event_hook(lv_event_t * e)
{

}

void dynamic_fmt_label_bind_text(lv_obj_t * dynamic_fmt_label, lv_subject_t * bind_text)
{
    lv_subject_add_observer_obj(bind_text, value_observer_cb, dynamic_fmt_label, dynamic_fmt_label);
    dynamic_fmt_label_t * cont = (dynamic_fmt_label_t *)dynamic_fmt_label;
    cont->bind_text = bind_text;
    update_value(cont);
}

void dynamic_fmt_label_bind_text_fmt(lv_obj_t * dynamic_fmt_label, lv_subject_t * bind_text_fmt)
{
    lv_subject_add_observer_obj(bind_text_fmt, fmt_observer_cb, dynamic_fmt_label, dynamic_fmt_label);
    dynamic_fmt_label_t * cont = (dynamic_fmt_label_t *)dynamic_fmt_label;
    cont->bind_text_fmt = bind_text_fmt;
    update_value(cont);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void update_value(dynamic_fmt_label_t * dynamic_fmt_label) {
    lv_subject_t * fmt_subject = dynamic_fmt_label->bind_text_fmt;
    lv_subject_t * val_subject = dynamic_fmt_label->bind_text;

    if (fmt_subject == NULL || val_subject == NULL) {
        return;
    }

    lv_subject_type_t fmt_type = fmt_subject->type;
    lv_subject_type_t val_type = val_subject->type;

    if (fmt_type == LV_SUBJECT_TYPE_INVALID || val_type == LV_SUBJECT_TYPE_INVALID) {
        return;
    }

    if (fmt_type != LV_SUBJECT_TYPE_STRING) {
        return;
    }

    const char * fmt = lv_subject_get_string(fmt_subject);

    switch(val_type) {
        case LV_SUBJECT_TYPE_INT: {
            int32_t val = lv_subject_get_int(val_subject);
            lv_label_set_text_fmt((lv_obj_t *)dynamic_fmt_label, fmt, val);
            break;
        }
        case LV_SUBJECT_TYPE_FLOAT: {
            float val = lv_subject_get_float(val_subject);
            lv_label_set_text_fmt((lv_obj_t *)dynamic_fmt_label, fmt, val);
            break;
        }
        default:
            LV_LOG_ERROR("Unimplemented subject type");
            break;
    }
}

static void value_observer_cb(lv_observer_t * obs, lv_subject_t * subject)
{
    update_value((dynamic_fmt_label_t *)lv_observer_get_user_data(obs));
}

static void fmt_observer_cb(lv_observer_t * obs, lv_subject_t * subject)
{
    update_value((dynamic_fmt_label_t *)lv_observer_get_user_data(obs));
}