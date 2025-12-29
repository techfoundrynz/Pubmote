/**
 * @file utilization_gauge_gen.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "utilization_gauge_private_gen.h"
#include "lvgl/src/core/lv_obj_class_private.h"
#include "pubmote_ui.h"

/*********************
 *      DEFINES
 *********************/
#define THICKNESS 24

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  GLOBAL PROTOTYPES
 **********************/

void utilization_gauge_constructor_hook(lv_obj_t * obj);
void utilization_gauge_destructor_hook(lv_obj_t * obj);
void utilization_gauge_event_hook(lv_event_t * e);

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void utilization_gauge_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void utilization_gauge_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void utilization_gauge_event(const lv_obj_class_t * class_p, lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t utilization_gauge_class = {
    .base_class = &lv_arc_class,
    .constructor_cb = utilization_gauge_constructor,
    .destructor_cb = utilization_gauge_destructor,
    .event_cb = utilization_gauge_event,
    .instance_size = sizeof(utilization_gauge_t),
    .editable = 1,
    .name = "utilization_gauge"
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * utilization_gauge_create(lv_obj_t * parent)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(&utilization_gauge_class, parent);
    lv_obj_class_init_obj(obj);

    return obj;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void utilization_gauge_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    utilization_gauge_t * widget = (utilization_gauge_t *)obj;
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
        lv_style_set_arc_rounded(&style_main, true);

        lv_style_init(&style_indicator);
        lv_style_set_arc_color(&style_indicator, SETTINGS_THEME_COLOR);
        lv_style_set_arc_width(&style_indicator, THICKNESS);
        lv_style_set_arc_rounded(&style_indicator, true);

        lv_style_init(&style_knob);
        lv_style_set_opa(&style_knob, (255 * 0 / 100));

        style_inited = true;
    }
    lv_arc_set_min_value(obj, 0);
    lv_arc_set_start_angle(obj, 120);
    lv_arc_set_bg_start_angle(obj, 120);
    lv_arc_set_end_angle(obj, 60);
    lv_arc_set_bg_end_angle(obj, 60);
    lv_obj_set_state(obj, LV_STATE_DISABLED, true);
    lv_arc_set_max_value(obj, 100);
    lv_arc_set_mode(obj, LV_ARC_MODE_REVERSE);

    lv_obj_add_style(obj, &style_main, 0);
    lv_obj_add_style(obj, &style_knob, LV_PART_KNOB);
    lv_obj_add_style(obj, &style_indicator, LV_PART_INDICATOR);


    utilization_gauge_constructor_hook(obj);

    LV_TRACE_OBJ_CREATE("finished");
}

static void utilization_gauge_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    utilization_gauge_destructor_hook(obj);
}

static void utilization_gauge_event(const lv_obj_class_t * class_p, lv_event_t * e)
{
    LV_UNUSED(class_p);

    lv_result_t res;

    /* Call the ancestor's event handler */
    res = lv_obj_event_base(&utilization_gauge_class, e);
    if(res != LV_RESULT_OK) return;

    utilization_gauge_event_hook(e);
}

