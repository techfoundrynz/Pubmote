/**
 * @file speed_gauge.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "speed_gauge_private_gen.h"
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

static void update_value(lv_obj_t * speed_gauge, float val);
static void value_observer_cb(lv_observer_t * obs, lv_subject_t * subject);
static void update_max_value(lv_obj_t * speed_gauge, float val);
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

void speed_gauge_constructor_hook(lv_obj_t * obj)
{

}

void speed_gauge_destructor_hook(lv_obj_t * obj)
{

}

void speed_gauge_event_hook(lv_event_t * e)
{

}

void speed_gauge_bind_speed(lv_obj_t * speed_gauge, lv_subject_t * bind_speed)
{
    lv_subject_add_observer_obj(bind_speed, value_observer_cb, speed_gauge, speed_gauge);
    // Update with current
    update_value(speed_gauge, lv_subject_get_float(bind_speed));
}

void speed_gauge_bind_max_speed(lv_obj_t * speed_gauge, lv_subject_t * bind_max_speed)
{
    lv_subject_add_observer_obj(bind_max_speed, max_value_observer_cb, speed_gauge, speed_gauge);
    // Update with current
    update_max_value(speed_gauge, lv_subject_get_float(bind_max_speed));
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void update_value(lv_obj_t * speed_gauge, float val) {
    lv_arc_set_value(speed_gauge, val);
}

static void value_observer_cb(lv_observer_t * obs, lv_subject_t * subject)
{
    update_value(lv_observer_get_user_data(obs), lv_subject_get_float(subject));
}

static void update_max_value(lv_obj_t * speed_gauge, float val) {
    lv_arc_set_range(speed_gauge, 0, val);
}

static void max_value_observer_cb(lv_observer_t * obs, lv_subject_t * subject)
{
    update_max_value(lv_observer_get_user_data(obs), lv_subject_get_float(subject));
}