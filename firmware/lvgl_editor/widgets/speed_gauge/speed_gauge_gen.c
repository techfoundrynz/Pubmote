/**
 * @file speed_gauge_gen.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "speed_gauge_private_gen.h"
#include "lvgl/src/core/lv_obj_class_private.h"
#include "pubmote_ui.h"

/*********************
 *      DEFINES
 *********************/
#define THICKNESS 32

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  GLOBAL PROTOTYPES
 **********************/

void speed_gauge_constructor_hook(lv_obj_t * obj);
void speed_gauge_destructor_hook(lv_obj_t * obj);
void speed_gauge_event_hook(lv_event_t * e);

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void speed_gauge_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void speed_gauge_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void speed_gauge_event(const lv_obj_class_t * class_p, lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t speed_gauge_class = {
    .base_class = &lv_arc_class,
    .constructor_cb = speed_gauge_constructor,
    .destructor_cb = speed_gauge_destructor,
    .event_cb = speed_gauge_event,
    .instance_size = sizeof(speed_gauge_t),
    .editable = 1,
    .name = "speed_gauge"
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * speed_gauge_create(lv_obj_t * parent)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(&speed_gauge_class, parent);
    lv_obj_class_init_obj(obj);

    return obj;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void speed_gauge_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    speed_gauge_t * widget = (speed_gauge_t *)obj;
    static lv_style_t style_main;
    static lv_style_t style_indicator;
    static lv_style_t style_knob;
    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&style_main);
        lv_style_set_arc_width(&style_main, THICKNESS);
        lv_style_set_arc_color(&style_main, lv_color_hex(0x878787));
        lv_style_set_width(&style_main, lv_pct(100));
        lv_style_set_height(&style_main, lv_pct(100));

        lv_style_init(&style_indicator);
        lv_style_set_arc_color(&style_indicator, SETTINGS_THEME_COLOR);
        lv_style_set_arc_width(&style_indicator, THICKNESS);

        lv_style_init(&style_knob);
        lv_style_set_opa(&style_knob, (255 * 0 / 100));

        style_inited = true;
    }
    lv_obj_t * lv_arc_0 = lv_arc_create(obj);
    lv_obj_set_name_static(lv_arc_0, "speed_gauge_#");
    lv_arc_set_min_value(lv_arc_0, 0);
    lv_arc_set_start_angle(lv_arc_0, 120);
    lv_arc_set_end_angle(lv_arc_0, 60);
    lv_obj_set_state(lv_arc_0, LV_STATE_DISABLED, true);

    lv_obj_add_style(lv_arc_0, &style_main, 0);
    lv_obj_add_style(lv_arc_0, &style_knob, LV_PART_KNOB);
    lv_obj_add_style(lv_arc_0, &style_indicator, LV_PART_INDICATOR);


    speed_gauge_constructor_hook(obj);

    LV_TRACE_OBJ_CREATE("finished");
}

static void speed_gauge_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    speed_gauge_destructor_hook(obj);
}

static void speed_gauge_event(const lv_obj_class_t * class_p, lv_event_t * e)
{
    LV_UNUSED(class_p);

    lv_result_t res;

    /* Call the ancestor's event handler */
    res = lv_obj_event_base(&speed_gauge_class, e);
    if(res != LV_RESULT_OK) return;

    speed_gauge_event_hook(e);
}

