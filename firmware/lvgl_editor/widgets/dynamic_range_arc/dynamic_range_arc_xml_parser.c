/**
 * @file dynamic_range_arc_xml_parser.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "dynamic_range_arc_gen.h"

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

void * dynamic_range_arc_xml_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = dynamic_range_arc_create(lv_xml_state_get_parent(state));

    if(item == NULL) {
        LV_LOG_ERROR("Failed to create dynamic_range_arc");
        return NULL;
    }

    return item;
}

void dynamic_range_arc_xml_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    void * item = lv_xml_state_get_item(state);

    lv_xml_obj_apply(state, attrs);

    for(int i = 0; attrs[i]; i += 2) {
        const char * name = attrs[i];
        const char * value = attrs[i + 1];
        if (lv_streq(name, "bind_value")) {
            lv_subject_t * subject = lv_xml_get_subject(&state->scope, value);
            if(subject) {
                dynamic_range_arc_bind_value(item, subject);
            } else { 
                LV_LOG_WARN("Subject \"%s\" not found for bind_value", value);
            }
        } else if (lv_streq(name, "bind_max_value")) {
            lv_subject_t * subject = lv_xml_get_subject(&state->scope, value);
            if(subject) {
                dynamic_range_arc_bind_max_value(item, subject);
            } else { 
                LV_LOG_WARN("Subject \"%s\" not found for bind_max_value", value);
            }
        }
    }
}

void dynamic_range_arc_register(void)
{
    lv_xml_register_widget("dynamic_range_arc", dynamic_range_arc_xml_create, dynamic_range_arc_xml_apply);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#endif /* LV_USE_XML */