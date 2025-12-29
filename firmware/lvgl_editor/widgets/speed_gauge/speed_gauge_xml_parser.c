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

        if(lv_streq(name, "bind_speed")) {
            lv_subject_t * subject = lv_xml_get_subject(&state->scope, value);
            if(subject) {
                speed_gauge_bind_speed(item, subject);
            } else { 
                LV_LOG_WARN("Subject \"%s\" not found for bind_speed", value);
            }
        } else if(lv_streq(name, "bind__maxspeed")) {
            lv_subject_t * subject = lv_xml_get_subject(&state->scope, value);
            if(subject) {
                speed_gauge_bind_max_speed(item, subject);
            } else { 
                LV_LOG_WARN("Subject \"%s\" not found for bind_max_speed", value);
            }
        }
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