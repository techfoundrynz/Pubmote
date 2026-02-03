/**
 * @file dynamic_range_arc.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "dynamic_range_arc_private_gen.h"
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

static void update_value(dynamic_range_arc_t * dynamic_range_arc);
static void value_observer_cb(lv_observer_t * obs, lv_subject_t * subject);
static void update_range(dynamic_range_arc_t * dynamic_range_arc);
static void max_value_observer_cb(lv_observer_t * obs, lv_subject_t * subject);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void dynamic_range_arc_constructor_hook(lv_obj_t * obj)
{

}

void dynamic_range_arc_destructor_hook(lv_obj_t * obj)
{

}

void dynamic_range_arc_event_hook(lv_event_t * e)
{

}

void dynamic_range_arc_bind_value(lv_obj_t * dynamic_range_arc, lv_subject_t * bind_value)
{
    lv_subject_add_observer_obj(bind_value, value_observer_cb, dynamic_range_arc, dynamic_range_arc);
    dynamic_range_arc_t * cont = (dynamic_range_arc_t *)dynamic_range_arc;
    cont->bind_value = bind_value;
    update_value(cont);
}

void dynamic_range_arc_bind_max_value(lv_obj_t * dynamic_range_arc, lv_subject_t * bind_max_value)
{
    lv_subject_add_observer_obj(bind_max_value, max_value_observer_cb, dynamic_range_arc, dynamic_range_arc);
    dynamic_range_arc_t * cont = (dynamic_range_arc_t *)dynamic_range_arc;
    cont->bind_max_value = bind_max_value;
    update_range(cont);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

 static void update_value(dynamic_range_arc_t * dynamic_range_arc) {
    lv_subject_t * val_subject = dynamic_range_arc->bind_value;

    if (val_subject == NULL) {
        return;
    }

    lv_subject_type_t val_type = val_subject->type;

    if (val_type != LV_SUBJECT_TYPE_FLOAT && val_type != LV_SUBJECT_TYPE_INT) {
        return;
    }

    lv_arc_set_value((lv_obj_t *)dynamic_range_arc, val_type == LV_SUBJECT_TYPE_FLOAT ? lv_subject_get_float(val_subject) : lv_subject_get_int(val_subject));
}

static void value_observer_cb(lv_observer_t * obs, lv_subject_t * subject)
{
    update_value((dynamic_range_arc_t *)lv_observer_get_user_data(obs));
}

 static void update_range(dynamic_range_arc_t * dynamic_range_arc) {
    lv_subject_t * max_subject = dynamic_range_arc->bind_max_value;

    if (max_subject == NULL) {
        return;
    }

    lv_subject_type_t max_type = max_subject->type;

    if (max_type != LV_SUBJECT_TYPE_FLOAT && max_type != LV_SUBJECT_TYPE_INT) {
        return;
    }

    int32_t min_val = lv_arc_get_min_value((lv_obj_t *)dynamic_range_arc);
    lv_arc_set_range((lv_obj_t *)dynamic_range_arc, min_val, max_type == LV_SUBJECT_TYPE_FLOAT ? lv_subject_get_float(max_subject) : lv_subject_get_int(max_subject));
}

static void max_value_observer_cb(lv_observer_t * obs, lv_subject_t * subject)
{
    update_range((dynamic_range_arc_t *)lv_observer_get_user_data(obs));
}