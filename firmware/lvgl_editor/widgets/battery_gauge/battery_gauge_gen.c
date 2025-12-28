/**
 * @file battery_gauge_gen.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "battery_gauge_private_gen.h"
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

void battery_gauge_constructor_hook(lv_obj_t * obj);
void battery_gauge_destructor_hook(lv_obj_t * obj);
void battery_gauge_event_hook(lv_event_t * e);

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void battery_gauge_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void battery_gauge_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void battery_gauge_event(const lv_obj_class_t * class_p, lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t battery_gauge_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = battery_gauge_constructor,
    .destructor_cb = battery_gauge_destructor,
    .event_cb = battery_gauge_event,
    .instance_size = sizeof(battery_gauge_t),
    .editable = 1,
    .name = "battery_gauge"
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * battery_gauge_create(lv_obj_t * parent)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(&battery_gauge_class, parent);
    lv_obj_class_init_obj(obj);

    return obj;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void battery_gauge_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    battery_gauge_t * widget = (battery_gauge_t *)obj;

    static bool style_inited = false;

    if (!style_inited) {

        style_inited = true;
    }
    lv_obj_set_width(obj, 100);
    lv_obj_set_height(obj, 40);
    lv_obj_set_style_bg_opa(obj, 0, 0);

    lv_obj_t * lv_obj_0 = lv_obj_create(obj);
    lv_obj_set_width(lv_obj_0, 95);
    lv_obj_set_height(lv_obj_0, 40);
    lv_obj_set_style_border_width(lv_obj_0, 4, 0);
    lv_obj_set_style_border_color(lv_obj_0, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(lv_obj_0, (255 * 0 / 100), 0);
    lv_obj_set_style_radius(lv_obj_0, 5, 0);
    lv_obj_set_style_pad_all(lv_obj_0, 0, 0);
    widget->lv_obj_0 = lv_obj_0;
    lv_obj_t * lv_obj_1 = lv_obj_create(lv_obj_0);
    lv_obj_set_style_radius(lv_obj_1, 0, 0);
    lv_obj_set_style_bg_color(lv_obj_1, lv_color_hex(0x42ca00), 0);
    lv_obj_set_height(lv_obj_1, lv_pct(100));
    lv_obj_set_width(lv_obj_1, lv_pct(100));
    lv_obj_set_style_bg_opa(lv_obj_1, (255 * 100 / 100), 0);
    lv_obj_set_style_border_width(lv_obj_1, 0, 0);
    lv_obj_set_align(lv_obj_1, LV_ALIGN_LEFT_MID);
    widget->lv_obj_1 = lv_obj_1;
    
    lv_obj_t * lv_obj_2 = lv_obj_create(obj);
    lv_obj_set_width(lv_obj_2, 5);
    lv_obj_set_height(lv_obj_2, 15);
    lv_obj_set_style_bg_color(lv_obj_2, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_align(lv_obj_2, LV_ALIGN_RIGHT_MID);
    lv_obj_set_style_border_width(lv_obj_2, 0, 0);
    lv_obj_set_style_radius(lv_obj_2, 0, 0);
    widget->lv_obj_2 = lv_obj_2;


    battery_gauge_constructor_hook(obj);

    LV_TRACE_OBJ_CREATE("finished");
}

static void battery_gauge_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    battery_gauge_destructor_hook(obj);
}

static void battery_gauge_event(const lv_obj_class_t * class_p, lv_event_t * e)
{
    LV_UNUSED(class_p);

    lv_result_t res;

    /* Call the ancestor's event handler */
    res = lv_obj_event_base(&battery_gauge_class, e);
    if(res != LV_RESULT_OK) return;

    battery_gauge_event_hook(e);
}

