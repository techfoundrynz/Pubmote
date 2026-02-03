/**
 * @file dynamic_range_arc_gen.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "dynamic_range_arc_private_gen.h"
#include "lvgl/src/core/lv_obj_class_private.h"
#include "pubmote_ui.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  GLOBAL PROTOTYPES
 **********************/

void dynamic_range_arc_constructor_hook(lv_obj_t * obj);
void dynamic_range_arc_destructor_hook(lv_obj_t * obj);
void dynamic_range_arc_event_hook(lv_event_t * e);

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void dynamic_range_arc_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void dynamic_range_arc_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void dynamic_range_arc_event(const lv_obj_class_t * class_p, lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t dynamic_range_arc_class = {
    .base_class = &lv_arc_class,
    .constructor_cb = dynamic_range_arc_constructor,
    .destructor_cb = dynamic_range_arc_destructor,
    .event_cb = dynamic_range_arc_event,
    .instance_size = sizeof(dynamic_range_arc_t),
    .editable = 1,
    .name = "dynamic_range_arc"
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * dynamic_range_arc_create(lv_obj_t * parent)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(&dynamic_range_arc_class, parent);
    lv_obj_class_init_obj(obj);

    return obj;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void dynamic_range_arc_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    dynamic_range_arc_t * widget = (dynamic_range_arc_t *)obj;

    static bool style_inited = false;

    if (!style_inited) {

        style_inited = true;
    }
    


    dynamic_range_arc_constructor_hook(obj);

    LV_TRACE_OBJ_CREATE("finished");
}

static void dynamic_range_arc_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    dynamic_range_arc_destructor_hook(obj);
}

static void dynamic_range_arc_event(const lv_obj_class_t * class_p, lv_event_t * e)
{
    LV_UNUSED(class_p);

    lv_result_t res;

    /* Call the ancestor's event handler */
    res = lv_obj_event_base(&dynamic_range_arc_class, e);
    if(res != LV_RESULT_OK) return;

    dynamic_range_arc_event_hook(e);
}

