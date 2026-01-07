/**
 * @file color_picker.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "color_picker_private_gen.h"
#include "pubmote_ui.h"

/*********************
 *      DEFINES
 *********************/

// Set to 1 to enable debug logging, 0 to disable
#define COLOR_PICKER_DEBUG 0

#if COLOR_PICKER_DEBUG
    #define CP_LOG(...) LV_LOG_USER(__VA_ARGS__)
#else
    #define CP_LOG(...) do {} while(0)
#endif

typedef enum {
    COLOR_PICKER_MODE_HUE = 0,
    COLOR_PICKER_MODE_SATURATION,
    COLOR_PICKER_MODE_LIGHTNESS
} color_picker_mode_t;

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    uint16_t hue;        // 0-360
    uint8_t saturation;  // 0-100
    uint8_t lightness;   // 0-100
    color_picker_mode_t mode;
    lv_subject_t * bind_value;
    int32_t press_start_value; // Track value when press starts
    bool is_dragging;          // Track if user is dragging
    bool mode_just_switched;   // Track if mode just changed (ignore next value change)
} color_picker_data_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void draw_gradient_dispatcher(lv_event_t * e);
static void draw_hue_gradient(lv_event_t * e);
static void draw_saturation_gradient(lv_event_t * e);
static void draw_lightness_gradient(lv_event_t * e);
static void update_knob_color(lv_event_t * e);
static void handle_long_press(lv_event_t * e);
static void handle_pressed(lv_event_t * e);
static void handle_released(lv_event_t * e);
static void update_color_subject(lv_obj_t * obj);
static uint32_t hsl_to_hex(uint16_t h, uint8_t s, uint8_t l);

/**********************
 *  STATIC VARIABLES
 **********************/

static lv_style_t knob_style;
static bool knob_style_inited = false;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void color_picker_constructor_hook(lv_obj_t * obj)
{
    // Allocate and initialize picker data
    color_picker_data_t * data = lv_malloc(sizeof(color_picker_data_t));
    data->hue = 0;
    data->saturation = 100;
    data->lightness = 100;
    data->mode = COLOR_PICKER_MODE_HUE;
    data->bind_value = NULL;
    data->press_start_value = 0;
    data->is_dragging = false;
    data->mode_just_switched = false;
    lv_obj_set_user_data(obj, data);
    
    // Initialize knob style for dynamic color updates
    // Only set the background color here - other properties (border, radius, padding) 
    // are defined in the XML and will be inherited
    if (!knob_style_inited) {
        lv_style_init(&knob_style);
        lv_style_set_bg_opa(&knob_style, LV_OPA_COVER);
        // Border, radius, and padding are set in XML
        knob_style_inited = true;
    }
    
    // Add the dynamic knob style
    lv_obj_add_style(obj, &knob_style, LV_PART_KNOB);
    
    // Enable sending of draw events
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    
    // Add event handler for custom drawing - dispatcher will call appropriate gradient
    lv_obj_add_event_cb(obj, draw_gradient_dispatcher, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    
    // Add event handler for value changes to update knob color
    lv_obj_add_event_cb(obj, update_knob_color, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Add event handlers for press tracking (to distinguish drag from long press)
    lv_obj_add_event_cb(obj, handle_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(obj, handle_released, LV_EVENT_RELEASED, NULL);
    
    // Add event handler for long press to toggle mode
    lv_obj_add_event_cb(obj, handle_long_press, LV_EVENT_LONG_PRESSED, NULL);
    
    // Set initial knob color (manually since no event yet)
    int32_t initial_value = lv_slider_get_value(obj);
    data->hue = initial_value % 360;
    lv_color_t initial_color = lv_color_hsv_to_rgb(data->hue, data->saturation, data->lightness);
    lv_style_set_bg_color(&knob_style, initial_color);
    
    CP_LOG("Color picker initialized - Mode: HUE, H:%d S:%d L:%d", 
                data->hue, data->saturation, data->lightness);
}

void color_picker_destructor_hook(lv_obj_t * obj)
{
    // Free allocated data
    color_picker_data_t * data = (color_picker_data_t *)lv_obj_get_user_data(obj);
    if (data) {
        lv_free(data);
    }
}

void color_picker_event_hook(lv_event_t * e)
{

}

void color_picker_set_size(lv_obj_t * color_picker, int32_t size)
{

}

void color_picker_bind_value(lv_obj_t * color_picker, lv_subject_t * bind_value)
{
    color_picker_data_t * data = (color_picker_data_t *)lv_obj_get_user_data(color_picker);
    if (data) {
        data->bind_value = bind_value;
        // Update subject with current color
        update_color_subject(color_picker);
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void draw_gradient_dispatcher(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    color_picker_data_t * data = (color_picker_data_t *)lv_obj_get_user_data(obj);
    if (!data) return;
    
    // Call the appropriate gradient drawing function based on mode
    switch (data->mode) {
        case COLOR_PICKER_MODE_HUE:
            draw_hue_gradient(e);
            break;
        case COLOR_PICKER_MODE_SATURATION:
            draw_saturation_gradient(e);
            break;
        case COLOR_PICKER_MODE_LIGHTNESS:
            draw_lightness_gradient(e);
            break;
    }
}

static void draw_hue_gradient(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    color_picker_data_t * data = (color_picker_data_t *)lv_obj_get_user_data(obj);
    
    CP_LOG("Drawing hue gradient - layer: %p", layer);
    
    // Get the slider's indicator (track) area specifically
    lv_area_t indicator_area;
    lv_obj_get_content_coords(obj, &indicator_area);
    
    // Adjust for padding
    lv_coord_t pad_left = lv_obj_get_style_pad_left(obj, LV_PART_MAIN);
    lv_coord_t pad_right = lv_obj_get_style_pad_right(obj, LV_PART_MAIN);
    lv_coord_t pad_top = lv_obj_get_style_pad_top(obj, LV_PART_MAIN);
    lv_coord_t pad_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN);
    
    indicator_area.x1 += pad_left;
    indicator_area.x2 -= pad_right;
    indicator_area.y1 += pad_top;
    indicator_area.y2 -= pad_bottom;
    
    // Draw gradient across the FULL width
    lv_coord_t width = lv_area_get_width(&indicator_area);
    lv_coord_t height = lv_area_get_height(&indicator_area);
    
    // Get the radius from the slider's style
    lv_coord_t track_radius = lv_obj_get_style_radius(obj, LV_PART_MAIN);
    
    // Draw gradient in segments
    const int segments = 120;
    
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_opa = LV_OPA_COVER;
    
    for (int i = 0; i < segments; i++) {
        // Calculate hue for this segment (0-360)
        uint16_t hue = (i * 360) / segments;
        
        // Use current saturation and lightness values
        uint8_t sat = data ? data->saturation : 100;
        uint8_t light = data ? data->lightness : 50;
        
        // Convert HSV to RGB using current S/L
        lv_color_t color = lv_color_hsv_to_rgb(hue, sat, light);
        
        // Create rectangle for this segment
        lv_area_t segment_area;
        segment_area.x1 = indicator_area.x1 + ((i * width) / segments);
        segment_area.x2 = indicator_area.x1 + (((i + 1) * width) / segments);
        segment_area.y1 = indicator_area.y1;
        segment_area.y2 = indicator_area.y2;
        
        // Set color for this segment
        rect_dsc.bg_color = color;
        
        // Apply radius to create rounded ends
        // Use full radius on first/last segments, but extend them slightly to ensure
        // the rounded corners are fully visible
        if (i == 0) {
            // First segment - left rounded edge
            rect_dsc.radius = track_radius;
            // Extend slightly to the right to ensure radius is visible
            segment_area.x2 = LV_MAX(segment_area.x2, segment_area.x1 + track_radius);
        } else if (i == segments - 1) {
            // Last segment - right rounded edge
            rect_dsc.radius = track_radius;
            // Extend slightly to the left to ensure radius is visible
            segment_area.x1 = LV_MIN(segment_area.x1, segment_area.x2 - track_radius);
        } else {
            // Middle segments - no radius
            rect_dsc.radius = 0;
        }
        
        // Draw using the layer
        lv_draw_rect(layer, &rect_dsc, &segment_area);
    }
}

static void update_knob_color(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    color_picker_data_t * data = (color_picker_data_t *)lv_obj_get_user_data(obj);
    if (!data) return;
    
    // Get current slider value
    int32_t value = lv_slider_get_value(obj);
    
    CP_LOG("update_knob_color: value=%d, press_start=%d, is_dragging=%d, mode_just_switched=%d", 
                value, data->press_start_value, data->is_dragging, data->mode_just_switched);
    
    // If mode just switched, REVERT the slider to the correct value and ignore this change
    if (data->mode_just_switched) {
        CP_LOG("Mode just switched - reverting slider value");
        
        // Revert to the correct value for this mode
        int32_t correct_value;
        switch (data->mode) {
            case COLOR_PICKER_MODE_HUE:
                correct_value = data->hue;
                break;
            case COLOR_PICKER_MODE_SATURATION:
                correct_value = data->saturation;
                break;
            case COLOR_PICKER_MODE_LIGHTNESS:
                correct_value = data->lightness;
                break;
            default:
                correct_value = 0;
                break;
        }
        
        // Set it back without triggering events
        lv_slider_set_value(obj, correct_value, LV_ANIM_OFF);
        CP_LOG("Reverted slider to correct value: %d", correct_value);
        return;
    }
    
    // Detect if user is dragging (value changed from press start)
    // This must come AFTER the mode_just_switched check
    if (value != data->press_start_value) {
        data->is_dragging = true;
        CP_LOG("Dragging detected: %d -> %d", data->press_start_value, value);
    }
    
    // Update the appropriate HSL component based on mode
    switch (data->mode) {
        case COLOR_PICKER_MODE_HUE:
            data->hue = value % 360;
            break;
        case COLOR_PICKER_MODE_SATURATION:
            data->saturation = value;
            break;
        case COLOR_PICKER_MODE_LIGHTNESS:
            data->lightness = value;
            break;
    }
    
    CP_LOG("Updated color: H=%d S=%d L=%d", data->hue, data->saturation, data->lightness);
    
    // Convert HSL to RGB for knob color
    lv_color_t color = lv_color_hsv_to_rgb(data->hue, data->saturation, data->lightness);
    
    // Update knob background color
    lv_style_set_bg_color(&knob_style, color);
    
    // Update the bound subject with the new color
    update_color_subject(obj);
    
    // Invalidate to trigger redraw
    lv_obj_invalidate(obj);
}

static void handle_long_press(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    color_picker_data_t * data = (color_picker_data_t *)lv_obj_get_user_data(obj);
    if (!data) return;
    
    // Only switch modes if the user wasn't dragging
    if (data->is_dragging) {
        CP_LOG("Long press ignored - user was dragging");
        return;
    }
    
    // CRITICAL: Stop this event from propagating to prevent slider from processing it
    lv_event_stop_bubbling(e);
    lv_event_stop_processing(e);
    
    // Cycle through modes
    data->mode = (data->mode + 1) % 3;
    
    // Set flag to ignore the next value change (from lv_slider_set_value below)
    data->mode_just_switched = true;
    
    // Reset dragging flag to prevent release from triggering update
    data->is_dragging = false;
    
    // CRITICAL: Clear the pressed state to prevent the release position from being used
    lv_obj_clear_state(obj, LV_STATE_PRESSED);
    
    // Update slider range based on mode
    switch (data->mode) {
        case COLOR_PICKER_MODE_HUE:
            lv_slider_set_range(obj, 0, 360);
            lv_slider_set_value(obj, data->hue, LV_ANIM_OFF);
            data->press_start_value = data->hue; // Update to prevent release from being seen as drag
            CP_LOG("Switched to HUE mode");
            break;
        case COLOR_PICKER_MODE_SATURATION:
            lv_slider_set_range(obj, 0, 100);
            lv_slider_set_value(obj, data->saturation, LV_ANIM_OFF);
            data->press_start_value = data->saturation; // Update to prevent release from being seen as drag
            CP_LOG("Switched to SATURATION mode");
            break;
        case COLOR_PICKER_MODE_LIGHTNESS:
            lv_slider_set_range(obj, 0, 100);
            lv_slider_set_value(obj, data->lightness, LV_ANIM_OFF);
            data->press_start_value = data->lightness; // Update to prevent release from being seen as drag
            CP_LOG("Switched to LIGHTNESS mode");
            break;
    }
    
    // Force redraw with new gradient
    lv_obj_invalidate(obj);
}

static void handle_pressed(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    color_picker_data_t * data = (color_picker_data_t *)lv_obj_get_user_data(obj);
    if (!data) return;
    
    // Record the starting value when press begins
    data->press_start_value = lv_slider_get_value(obj);
    data->is_dragging = false;
}

static void handle_released(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    color_picker_data_t * data = (color_picker_data_t *)lv_obj_get_user_data(obj);
    if (!data) return;
    
    // Reset dragging flag on release
    data->is_dragging = false;
    
    // Clear mode_just_switched flag on release (in case mode was switched)
    // This ensures all value changes during the mode switch are ignored
    if (data->mode_just_switched) {
        data->mode_just_switched = false;
        CP_LOG("Cleared mode_just_switched flag on release");
    }
}

static void draw_saturation_gradient(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    color_picker_data_t * data = (color_picker_data_t *)lv_obj_get_user_data(obj);
    lv_layer_t * layer = lv_event_get_layer(e);
    if (!data) return;
    
    lv_area_t area;
    lv_obj_get_content_coords(obj, &area);
    
    lv_coord_t pad_left = lv_obj_get_style_pad_left(obj, LV_PART_MAIN);
    lv_coord_t pad_right = lv_obj_get_style_pad_right(obj, LV_PART_MAIN);
    lv_coord_t pad_top = lv_obj_get_style_pad_top(obj, LV_PART_MAIN);
    lv_coord_t pad_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN);
    
    area.x1 += pad_left;
    area.x2 -= pad_right;
    area.y1 += pad_top;
    area.y2 -= pad_bottom;
    
    lv_coord_t width = lv_area_get_width(&area);
    lv_coord_t track_radius = lv_obj_get_style_radius(obj, LV_PART_MAIN);
    
    const int segments = 100;
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_opa = LV_OPA_COVER;
    
    for (int i = 0; i < segments; i++) {
        uint8_t sat = (i * 100) / segments;
        lv_color_t color = lv_color_hsv_to_rgb(data->hue, sat, data->lightness);
        
        lv_area_t segment_area;
        segment_area.x1 = area.x1 + ((i * width) / segments);
        segment_area.x2 = area.x1 + (((i + 1) * width) / segments);
        segment_area.y1 = area.y1;
        segment_area.y2 = area.y2;
        
        rect_dsc.bg_color = color;
        
        // Apply radius to create rounded ends
        if (i == 0) {
            rect_dsc.radius = track_radius;
            segment_area.x2 = LV_MAX(segment_area.x2, segment_area.x1 + track_radius);
        } else if (i == segments - 1) {
            rect_dsc.radius = track_radius;
            segment_area.x1 = LV_MIN(segment_area.x1, segment_area.x2 - track_radius);
        } else {
            rect_dsc.radius = 0;
        }
        
        lv_draw_rect(layer, &rect_dsc, &segment_area);
    }
}

static void draw_lightness_gradient(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    color_picker_data_t * data = (color_picker_data_t *)lv_obj_get_user_data(obj);
    lv_layer_t * layer = lv_event_get_layer(e);
    if (!data) return;
    
    lv_area_t area;
    lv_obj_get_content_coords(obj, &area);
    
    lv_coord_t pad_left = lv_obj_get_style_pad_left(obj, LV_PART_MAIN);
    lv_coord_t pad_right = lv_obj_get_style_pad_right(obj, LV_PART_MAIN);
    lv_coord_t pad_top = lv_obj_get_style_pad_top(obj, LV_PART_MAIN);
    lv_coord_t pad_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN);
    
    area.x1 += pad_left;
    area.x2 -= pad_right;
    area.y1 += pad_top;
    area.y2 -= pad_bottom;
    
    lv_coord_t width = lv_area_get_width(&area);
    lv_coord_t track_radius = lv_obj_get_style_radius(obj, LV_PART_MAIN);
    
    const int segments = 100;
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_opa = LV_OPA_COVER;
    
    for (int i = 0; i < segments; i++) {
        uint8_t light = (i * 100) / segments;
        lv_color_t color = lv_color_hsv_to_rgb(data->hue, data->saturation, light);
        
        lv_area_t segment_area;
        segment_area.x1 = area.x1 + ((i * width) / segments);
        segment_area.x2 = area.x1 + (((i + 1) * width) / segments);
        segment_area.y1 = area.y1;
        segment_area.y2 = area.y2;
        
        rect_dsc.bg_color = color;
        
        // Apply radius to create rounded ends
        if (i == 0) {
            rect_dsc.radius = track_radius;
            segment_area.x2 = LV_MAX(segment_area.x2, segment_area.x1 + track_radius);
        } else if (i == segments - 1) {
            rect_dsc.radius = track_radius;
            segment_area.x1 = LV_MIN(segment_area.x1, segment_area.x2 - track_radius);
        } else {
            rect_dsc.radius = 0;
        }
        
        lv_draw_rect(layer, &rect_dsc, &segment_area);
    }
}

static void update_color_subject(lv_obj_t * obj)
{
    color_picker_data_t * data = (color_picker_data_t *)lv_obj_get_user_data(obj);
    if (!data || !data->bind_value) return;
    
    // Convert HSL to hex color
    uint32_t hex_color = hsl_to_hex(data->hue, data->saturation, data->lightness);
    
    // Update the subject with the hex color value
    lv_subject_set_int(data->bind_value, hex_color);
}

static uint32_t hsl_to_hex(uint16_t h, uint8_t s, uint8_t l)
{
    // Convert HSL to RGB using LVGL's function
    lv_color_t rgb = lv_color_hsv_to_rgb(h, s, l);
    
    // Convert to hex format (0xRRGGBB)
    return lv_color_to_u32(rgb) & 0xFFFFFF;
}