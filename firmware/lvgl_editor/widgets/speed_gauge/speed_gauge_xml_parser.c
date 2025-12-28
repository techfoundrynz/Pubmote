/**
 * @file speed_gauge_xml_parser.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "speed_gauge_gen.h"

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "src/lvgl_private.h"
#else
    #include "lvgl/src/lvgl_private.h"
#endif

#if LV_USE_XML

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void * speed_gauge_xml_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = speed_gauge_create(lv_xml_state_get_parent(state));

    if(item == NULL) {
        LV_LOG_ERROR("Failed to create speed_gauge");
        return NULL;
    }

    return item;
}

void speed_gauge_xml_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    void * item = lv_xml_state_get_item(state);

    lv_xml_obj_apply(state, attrs);

    for(int i = 0; attrs[i]; i += 2) {
        const char * name = attrs[i];
        const char * value = attrs[i + 1];
    }
}

void speed_gauge_register(void)
{
    lv_xml_register_widget("speed_gauge", speed_gauge_xml_create, speed_gauge_xml_apply);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#endif /* LV_USE_XML */