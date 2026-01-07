// Helper functions for color picker - to be appended to color_picker.c

static void update_knob_color(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    color_picker_data_t * data = (color_picker_data_t *)lv_obj_get_user_data(obj);
    if (!data) return;
    
    // Get current slider value
    int32_t value = lv_slider_get_value(obj);
    
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
    
    // Cycle through modes
    data->mode = (data->mode + 1) % 3;
    
    // Update slider range based on mode
    switch (data->mode) {
        case COLOR_PICKER_MODE_HUE:
            lv_slider_set_range(obj, 0, 360);
            lv_slider_set_value(obj, data->hue, LV_ANIM_OFF);
            LV_LOG_USER("Switched to HUE mode");
            break;
        case COLOR_PICKER_MODE_SATURATION:
            lv_slider_set_range(obj, 0, 100);
            lv_slider_set_value(obj, data->saturation, LV_ANIM_OFF);
            LV_LOG_USER("Switched to SATURATION mode");
            break;
        case COLOR_PICKER_MODE_LIGHTNESS:
            lv_slider_set_range(obj, 0, 100);
            lv_slider_set_value(obj, data->lightness, LV_ANIM_OFF);
            LV_LOG_USER("Switched to LIGHTNESS mode");
            break;
    }
    
    // Force redraw with new gradient
    lv_obj_invalidate(obj);
}

static void draw_saturation_gradient(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    color_picker_data_t * data = (color_picker_data_t *)lv_obj_get_user_data(obj);
    lv_layer_t * layer = lv_event_get_layer(e);
    if (!data) return;
    
    // Get drawing area
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
        rect_dsc.radius = (i == 0 || i == segments - 1) ? track_radius : 0;
        
        lv_draw_rect(layer, &rect_dsc, &segment_area);
    }
}

static void draw_lightness_gradient(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    color_picker_data_t * data = (color_picker_data_t *)lv_obj_get_user_data(obj);
    lv_layer_t * layer = lv_event_get_layer(e);
    if (!data) return;
    
    // Get drawing area
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
        rect_dsc.radius = (i == 0 || i == segments - 1) ? track_radius : 0;
        
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
