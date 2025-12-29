/**
 * @file dynamic_fmt_label_xml_parser.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "dynamic_fmt_label_gen.h"

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

void * dynamic_fmt_label_xml_create(lv_xml_parser_state_t * state, const char ** attrs)
{
    LV_UNUSED(attrs);
    void * item = dynamic_fmt_label_create(lv_xml_state_get_parent(state));

    if(item == NULL) {
        LV_LOG_ERROR("Failed to create dynamic_fmt_label");
        return NULL;
    }

    return item;
}

void dynamic_fmt_label_xml_apply(lv_xml_parser_state_t * state, const char ** attrs)
{
    void * item = lv_xml_state_get_item(state);

    lv_xml_obj_apply(state, attrs);

    for(int i = 0; attrs[i]; i += 2) {
        const char * name = attrs[i];
        const char * value = attrs[i + 1];

        if (lv_streq(name, "bind_text")) {
            lv_subject_t * subject = lv_xml_get_subject(&state->scope, value);
            if(subject) {
                dynamic_fmt_label_bind_text(item, subject);
            } else { 
                LV_LOG_WARN("Subject \"%s\" not found for bind_text", value);
            }
        } else if (lv_streq(name, "bind_text_fmt")) {
            lv_subject_t * subject = lv_xml_get_subject(&state->scope, value);
            if(subject) {
                dynamic_fmt_label_bind_text_fmt(item, subject);
            } else { 
                LV_LOG_WARN("Subject \"%s\" not found for bind_text_fmt", value);
            }
        }
    }
}

void dynamic_fmt_label_register(void)
{
    lv_xml_register_widget("dynamic_fmt_label", dynamic_fmt_label_xml_create, dynamic_fmt_label_xml_apply);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#endif /* LV_USE_XML */