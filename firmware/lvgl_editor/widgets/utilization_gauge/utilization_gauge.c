/**
 * @file utilization_gauge.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "utilization_gauge_private_gen.h"
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

static void update_value(lv_obj_t * utilization_gauge, int val);
static void value_observer_cb(lv_observer_t * obs, lv_subject_t * subject);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void utilization_gauge_constructor_hook(lv_obj_t * obj)
{

}

void utilization_gauge_destructor_hook(lv_obj_t * obj)
{

}

void utilization_gauge_event_hook(lv_event_t * e)
{

}

void utilization_gauge_bind_duty_cycle(lv_obj_t * utilization_gauge, lv_subject_t * bind_duty_cycle)
{
    lv_subject_add_observer_obj(bind_duty_cycle, value_observer_cb, utilization_gauge, utilization_gauge);
    // Update with current
    update_value(utilization_gauge, lv_subject_get_int(bind_duty_cycle));
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void update_value(lv_obj_t * utilization_gauge, int val) {
    lv_arc_set_value(utilization_gauge, val);
}

static void value_observer_cb(lv_observer_t * obs, lv_subject_t * subject)
{
    update_value(lv_observer_get_user_data(obs), lv_subject_get_int(subject));
}