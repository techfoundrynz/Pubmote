/**
 * @file color_picker_gen.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "color_picker_private_gen.h"
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

void color_picker_constructor_hook(lv_obj_t * obj);
void color_picker_destructor_hook(lv_obj_t * obj);
void color_picker_event_hook(lv_event_t * e);

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void color_picker_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void color_picker_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void color_picker_event(const lv_obj_class_t * class_p, lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t color_picker_class = {
    .base_class = &lv_slider_class,
    .constructor_cb = color_picker_constructor,
    .destructor_cb = color_picker_destructor,
    .event_cb = color_picker_event,
    .instance_size = sizeof(color_picker_t),
    .editable = 1,
    .name = "color_picker"
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * color_picker_create(lv_obj_t * parent)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(&color_picker_class, parent);
    lv_obj_class_init_obj(obj);

    return obj;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void color_picker_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    color_picker_t * widget = (color_picker_t *)obj;
    static lv_style_t style_main;
    static lv_style_t style_track;
    static lv_style_t style_knob;
    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&style_main);
        lv_style_set_bg_color(&style_main, THEME_STRUCTURE5);
        lv_style_set_bg_opa(&style_main, OPA_MUTED);
        lv_style_set_radius(&style_main, 20);
        lv_style_set_height(&style_main, UNIT_MD);

        lv_style_init(&style_track);
        lv_style_set_bg_opa(&style_track, (255 * 0 / 100));
        lv_style_set_radius(&style_track, 20);

        lv_style_init(&style_knob);
        lv_style_set_bg_opa(&style_knob, (255 * 100 / 100));
        lv_style_set_border_width(&style_knob, 2);
        lv_style_set_border_color(&style_knob, THEME_STRUCTURE5);
        lv_style_set_border_opa(&style_knob, (255 * 100 / 100));
        lv_style_set_radius(&style_knob, 20);
        lv_style_set_pad_all(&style_knob, UNIT_SM);

        style_inited = true;
    }
    lv_obj_set_width(obj, lv_pct(100));
    lv_obj_set_style_min_width(obj, 200, 0);
    lv_slider_set_min_value(obj, 0);
    lv_slider_set_max_value(obj, 360);

    lv_obj_remove_style_all(obj);
    lv_obj_add_style(obj, &style_main, 0);
    lv_obj_add_style(obj, &style_track, LV_PART_INDICATOR);
    lv_obj_add_style(obj, &style_knob, LV_PART_KNOB);


    color_picker_constructor_hook(obj);

    LV_TRACE_OBJ_CREATE("finished");
}

static void color_picker_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    color_picker_destructor_hook(obj);
}

static void color_picker_event(const lv_obj_class_t * class_p, lv_event_t * e)
{
    LV_UNUSED(class_p);

    lv_result_t res;

    /* Call the ancestor's event handler */
    res = lv_obj_event_base(&color_picker_class, e);
    if(res != LV_RESULT_OK) return;

    color_picker_event_hook(e);
}

