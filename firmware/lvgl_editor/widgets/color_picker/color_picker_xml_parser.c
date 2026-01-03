/**
 * @file color_picker_xml_parser.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "color_picker_gen.h"

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

void * color_picker_xml_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = color_picker_create(lv_xml_state_get_parent(state));

    if(item == NULL) {
        LV_LOG_ERROR("Failed to create color_picker");
        return NULL;
    }

    return item;
}

void color_picker_xml_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    void * item = lv_xml_state_get_item(state);

    lv_xml_obj_apply(state, attrs);

    for(int i = 0; attrs[i]; i += 2) {
        const char * name = attrs[i];
        const char * value = attrs[i + 1];
        
        if (lv_streq(name, "bind_value")) {
            lv_subject_t * subject = lv_xml_get_subject(&state->scope, value);
            if(subject) {
                color_picker_bind_value(item, subject);
            } else { 
                LV_LOG_WARN("Subject \"%s\" not found for bind_value", value);
            }
        } else if (lv_streq(name, "size")) {
            int32_t size_val = lv_xml_atoi(value);
            color_picker_set_size(item, size_val);
        }
    }
}

void color_picker_register(void)
{
    lv_xml_register_widget("color_picker", color_picker_xml_create, color_picker_xml_apply);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#endif /* LV_USE_XML */