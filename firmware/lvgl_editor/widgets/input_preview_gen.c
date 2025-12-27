/**
 * @file input_preview_gen.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "input_preview_private_gen.h"
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

void input_preview_constructor_hook(lv_obj_t * obj);
void input_preview_destructor_hook(lv_obj_t * obj);
void input_preview_event_hook(lv_event_t * e);

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void input_preview_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void input_preview_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void input_preview_event(const lv_obj_class_t * class_p, lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t input_preview_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = input_preview_constructor,
    .destructor_cb = input_preview_destructor,
    .event_cb = input_preview_event,
    .instance_size = sizeof(input_preview_t),
    .editable = 1,
    .name = "input_preview"
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * input_preview_create(lv_obj_t * parent)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(&input_preview_class, parent);
    lv_obj_class_init_obj(obj);

    return obj;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void input_preview_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    input_preview_t * widget = (input_preview_t *)obj;

    static bool style_inited = false;

    if (!style_inited) {

        style_inited = true;
    }
    lv_obj_remove_style_all(obj);
    lv_obj_t * lv_obj_0 = lv_obj_create(obj);
    lv_obj_set_width(lv_obj_0, lv_pct(90));
    lv_obj_set_height(lv_obj_0, lv_pct(90));
    lv_obj_set_style_border_width(lv_obj_0, 4, 0);
    lv_obj_set_style_border_color(lv_obj_0, lv_color_hex(0x000000), 0);
    lv_obj_set_style_radius(lv_obj_0, lv_pct(50), 0);
    lv_obj_set_align(lv_obj_0, LV_ALIGN_CENTER);
    widget->lv_obj_0 = lv_obj_0;
    
    lv_obj_t * lv_obj_1 = lv_obj_create(obj);
    lv_obj_set_height(lv_obj_1, 4);
    lv_obj_set_width(lv_obj_1, lv_pct(100));
    lv_obj_set_style_bg_color(lv_obj_1, lv_color_hex(0xEE4266), 0);
    lv_obj_set_align(lv_obj_1, LV_ALIGN_CENTER);
    widget->lv_obj_1 = lv_obj_1;
    
    lv_obj_t * lv_obj_2 = lv_obj_create(obj);
    lv_obj_set_height(lv_obj_2, lv_pct(100));
    lv_obj_set_width(lv_obj_2, 4);
    lv_obj_set_style_bg_color(lv_obj_2, lv_color_hex(0xEE4266), 0);
    lv_obj_set_align(lv_obj_2, LV_ALIGN_CENTER);
    widget->lv_obj_2 = lv_obj_2;
    
    lv_obj_t * lv_obj_3 = lv_obj_create(obj);
    lv_obj_set_width(lv_obj_3, lv_pct(40));
    lv_obj_set_height(lv_obj_3, lv_pct(40));
    lv_obj_set_style_border_color(lv_obj_3, lv_color_hex(0x414141), 0);
    lv_obj_set_style_border_width(lv_obj_3, 4, 0);
    lv_obj_set_style_bg_opa(lv_obj_3, 0, 0);
    lv_obj_set_style_radius(lv_obj_3, lv_pct(50), 0);
    lv_obj_set_align(lv_obj_3, LV_ALIGN_CENTER);
    widget->lv_obj_3 = lv_obj_3;
    
    lv_obj_t * lv_obj_4 = lv_obj_create(obj);
    lv_obj_set_height(lv_obj_4, lv_pct(20));
    lv_obj_set_width(lv_obj_4, lv_pct(20));
    lv_obj_set_align(lv_obj_4, LV_ALIGN_CENTER);
    lv_obj_set_style_border_width(lv_obj_4, 0, 0);
    lv_obj_set_style_bg_opa(lv_obj_4, 0, 0);
    widget->lv_obj_4 = lv_obj_4;
    lv_obj_t * lv_obj_5 = lv_obj_create(lv_obj_4);
    lv_obj_set_height(lv_obj_5, lv_pct(20));
    lv_obj_set_width(lv_obj_5, 4);
    lv_obj_set_style_bg_color(lv_obj_5, lv_color_hex(0xff0000), 0);
    lv_obj_set_align(lv_obj_5, LV_ALIGN_CENTER);
    widget->lv_obj_5 = lv_obj_5;
    
    lv_obj_t * lv_obj_6 = lv_obj_create(lv_obj_4);
    lv_obj_set_height(lv_obj_6, 4);
    lv_obj_set_width(lv_obj_6, lv_pct(20));
    lv_obj_set_style_bg_color(lv_obj_6, lv_color_hex(0xff0000), 0);
    lv_obj_set_align(lv_obj_6, LV_ALIGN_CENTER);
    widget->lv_obj_6 = lv_obj_6;


    input_preview_constructor_hook(obj);

    LV_TRACE_OBJ_CREATE("finished");
}

static void input_preview_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    input_preview_destructor_hook(obj);
}

static void input_preview_event(const lv_obj_class_t * class_p, lv_event_t * e)
{
    LV_UNUSED(class_p);

    lv_result_t res;

    /* Call the ancestor's event handler */
    res = lv_obj_event_base(&input_preview_class, e);
    if(res != LV_RESULT_OK) return;

    input_preview_event_hook(e);
}

