#include "screen_utils.h"
#include "config.h"
#include "lvgl.h"
#include "number_utils.h"
#include "remote/display.h"
#include <ui/ui.h>

static void scale_padding(lv_obj_t *obj) {
  if (obj == NULL) {
    return;
  }

  // Get current padding values
  lv_coord_t left = lv_obj_get_style_pad_left(obj, 0);
  lv_coord_t right = lv_obj_get_style_pad_right(obj, 0);
  lv_coord_t top = lv_obj_get_style_pad_top(obj, 0);
  lv_coord_t bottom = lv_obj_get_style_pad_bottom(obj, 0);

  // Scale and set new padding values using SCALE_FACTOR
  lv_obj_set_style_pad_left(obj, (lv_coord_t)(left * SCALE_FACTOR * SCALE_PADDING), LV_STATE_DEFAULT);
  lv_obj_set_style_pad_right(obj, (lv_coord_t)(right * SCALE_FACTOR * SCALE_PADDING), LV_STATE_DEFAULT);
  lv_obj_set_style_pad_top(obj, (lv_coord_t)(top * SCALE_FACTOR * SCALE_PADDING), LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(obj, (lv_coord_t)(bottom * SCALE_FACTOR * SCALE_PADDING), LV_STATE_DEFAULT);
}

static void scale_border(lv_obj_t *obj) {
  if (obj == NULL || lv_obj_check_type(obj, &lv_slider_class)) {
    return;
  }

  // Border size
  // Get current border size
  lv_coord_t border_width = lv_obj_get_style_border_width(obj, 0);

  // Scale and set new border size using SCALE_FACTOR
  lv_obj_set_style_border_width(obj, (lv_coord_t)(border_width * SCALE_FACTOR), LV_STATE_DEFAULT);

  // Border radius
  // Get current border radius
  lv_coord_t radius = lv_obj_get_style_radius(obj, 0);

  // Scale and set new border radius using SCALE_FACTOR
  lv_obj_set_style_radius(obj, (lv_coord_t)(radius * SCALE_FACTOR), LV_STATE_DEFAULT);
}

static void scale_arc(lv_obj_t *obj) {
  if (obj == NULL || !lv_obj_check_type(obj, &lv_arc_class))
    return;

  // Get current arc width
  lv_coord_t width = lv_obj_get_style_arc_width(obj, 0);

  // Scale and set new arc width using SCALE_FACTOR
  lv_obj_set_style_arc_width(obj, (lv_coord_t)(width * SCALE_FACTOR), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_width(obj, (lv_coord_t)(width * SCALE_FACTOR), LV_PART_INDICATOR | LV_STATE_DEFAULT);
}

static void scale_dimensions(lv_obj_t *obj) {
  if (obj == NULL) {
    return;
  }

  // Array of parts to process
  uint32_t parts[] = {
      LV_PART_MAIN,
      // LV_PART_KNOB,
      // LV_PART_INDICATOR,
      // LV_PART_SELECTED
  };

  for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
    uint32_t part = parts[i];

    lv_coord_t width = lv_obj_get_style_width(obj, part);
    lv_coord_t height = lv_obj_get_style_height(obj, part);

    if (width != NULL) {
      if (!LV_COORD_IS_PCT(width) && width != LV_SIZE_CONTENT) {
        lv_obj_set_width(obj, (lv_coord_t)(width * SCALE_FACTOR));
      }
    }

    if (height != NULL) {
      if (!LV_COORD_IS_PCT(height) && height != LV_SIZE_CONTENT) {
        lv_obj_set_height(obj, (lv_coord_t)(height * SCALE_FACTOR));
      }
    }
  }
}

static bool is_bold_font(lv_font_t *font) {
  if (font == &ui_font_Inter_Bold_14 || font == &ui_font_Inter_Bold_28 || font == &ui_font_Inter_Bold_48 ||
      font == &ui_font_Inter_Bold_96) {
    return true;
  }

  return false;
}

static uint8_t get_font_size(lv_font_t *font) {
  if (font == &ui_font_Inter_14 || font == &ui_font_Inter_Bold_14) {
    return 14;
  }
  else if (font == &ui_font_Inter_28 || font == &ui_font_Inter_Bold_28) {
    return 28;
  }
  else if (font == &ui_font_Inter_Bold_48) {
    return 48;
  }
  else if (font == &ui_font_Inter_Bold_96) {
    return 96;
  }
  return 14;
}

void scale_text(lv_obj_t *obj) {
  if (obj == NULL || !(lv_obj_check_type(obj, &lv_label_class) || lv_obj_check_type(obj, &lv_dropdown_class) ||
                       lv_obj_check_type(obj, &lv_dropdownlist_class))) {
    return;
  }

  uint32_t parts[] = {LV_PART_MAIN, LV_PART_ITEMS, LV_PART_SELECTED};

  for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
    uint32_t part = parts[i];
    lv_font_t *font = lv_obj_get_style_text_font(obj, part);

    if (font == NULL) {
      continue;
    }

    int size = get_font_size(font) * SCALE_FACTOR * SCALE_FONT;
    bool is_bold = is_bold_font(font);

    lv_font_t *new_font = font;
    if (size <= 20) {
      if (is_bold) {
        new_font = &ui_font_Inter_Bold_14;
      }
      else {
        new_font = &ui_font_Inter_28;
      }
    }
    else if (size <= 36) {
      if (is_bold) {
        new_font = &ui_font_Inter_Bold_28;
      }
      else {
        new_font = &ui_font_Inter_28;
      }
    }
    else if (size <= 64) {
      if (is_bold) {
        new_font = &ui_font_Inter_Bold_48;
      }
      else {
        new_font = &ui_font_Inter_28;
      }
    }
    else {
      if (is_bold) {
        new_font = &ui_font_Inter_Bold_96;
      }
      else {
        new_font = &ui_font_Inter_28;
      }
    }

    lv_obj_set_style_text_font(obj, new_font, part);
  }
}

static void scale_position(lv_obj_t *obj) {
  if (obj == NULL) {
    return;
  }

  uint32_t parts[] = {
      LV_PART_MAIN,
      // LV_PART_ITEMS,
      // LV_PART_SELECTED
  };

  for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
    uint32_t part = parts[i];

    lv_coord_t x = lv_obj_get_style_x(obj, part);
    lv_coord_t y = lv_obj_get_style_y(obj, part);

    if (!LV_COORD_IS_PCT(x)) {
      lv_obj_set_x(obj, (lv_coord_t)(x * SCALE_FACTOR));
    }

    if (!LV_COORD_IS_PCT(y)) {
      lv_obj_set_y(obj, (lv_coord_t)(y * SCALE_FACTOR));
    }
  }
}

/**
 * Recursively process all children of an LVGL object
 * @param parent The parent object to start from
 * @param callback Function to call for each child
 */
static void process_children_recursive(lv_obj_t *parent, void (*callback)(lv_obj_t *obj)) {
  if (parent == NULL) {
    return;
  }

  uint32_t child_cnt = lv_obj_get_child_cnt(parent);

  for (uint32_t i = 0; i < child_cnt; i++) {
    lv_obj_t *child = lv_obj_get_child(parent, i);
    if (child != NULL) {
      // Process this child
      callback(child);

      // Recursively process its children
      process_children_recursive(child, callback);
    }
  }
}

static void scale_element(lv_obj_t *element) {
  scale_padding(element);
  scale_border(element);
  scale_dimensions(element);
  scale_position(element);
  scale_text(element);
  scale_arc(element);

  // Mark the style as modified
  lv_obj_mark_layout_as_dirty(element);
  lv_obj_refresh_style(element, LV_STYLE_PROP_ANY, LV_PART_MAIN);
}

void apply_ui_scale(lv_obj_t *element) {
  if (SCALE_FACTOR == 1.0 && SCALE_FONT == 1.0) {
    return;
  }

  if (element != NULL) {
    process_children_recursive(element, scale_element);
    return;
  }

  if (element == NULL) {
    process_children_recursive(lv_scr_act(), scale_element);
    return;
  }
}

void reload_screens() {
  if (LVGL_lock(-1)) {
    lv_obj_clean(ui_MenuScreen);
    lv_obj_clean(ui_StatsScreen);
    ui_MenuScreen_screen_init();
    ui_StatsScreen_screen_init();
    apply_ui_scale(ui_StatsScreen);
    apply_ui_scale(ui_MenuScreen);
    LVGL_unlock();
  }
}

static lv_group_t *navigation_group = NULL;

lv_group_t *create_navigation_group(lv_obj_t *container) {
  if (navigation_group == NULL) {
    navigation_group = lv_group_create();
    // https://docs.lvgl.io/8.0/overview/indev.html#default-group
    lv_group_set_default(navigation_group);
    lv_indev_set_group(get_encoder(), navigation_group);
  }
  else {
    // If a group already exists, clear it to remove old objects
    lv_group_remove_all_objs(navigation_group);
  }

  // Iterate children of container and add them to the group
  uint32_t childrenCount = lv_obj_get_child_cnt(container);
  lv_obj_t *firstChild = NULL;
  for (uint32_t i = 0; i < childrenCount; i++) {
    lv_obj_t *child = lv_obj_get_child(container, i);
    if (child != NULL) {
      lv_group_add_obj(navigation_group, child);

      if (i == 0) {
        firstChild = child;
      }
    }
  }

  if (JOYSTICK_ENABLED && firstChild != NULL) {
    lv_group_focus_obj(firstChild);
  }

  return navigation_group;
}

void add_page_scroll_indicators(lv_obj_t *header_item, lv_obj_t *body_item) {
  // How many scroll icons already exist
  uint32_t total_scroll_icons = lv_obj_get_child_cnt(header_item);
  uint32_t total_settings = lv_obj_get_child_cnt(body_item);
  // Set spacing between icons
  lv_obj_set_style_pad_column(header_item, 3 * SCALE_FACTOR, LV_PART_MAIN | LV_STATE_DEFAULT);

  for (uint32_t i = 0; i < total_settings; i++) {
    uint8_t bg_opacity = i == 0 ? 255 : 100;

    // Either get existing settings header icons or create them if needed
    lv_obj_t *item = total_scroll_icons == 0 ? lv_obj_create(header_item) : lv_obj_get_child(header_item, i);

    lv_obj_remove_style_all(item);
    lv_obj_set_width(item, 6 * SCALE_FACTOR);
    lv_obj_set_height(item, 6 * SCALE_FACTOR);
    lv_obj_set_align(item, LV_ALIGN_CENTER);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_set_style_radius(item, 3 * SCALE_FACTOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(item, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(item, bg_opacity, LV_PART_MAIN | LV_STATE_DEFAULT);
  }
}

void paged_scroll_event_cb(lv_event_t *e) {
  lv_obj_t *cont = lv_event_get_target(e);
  uint8_t total_items = lv_obj_get_child_cnt(cont);
  lv_obj_t *indicator_target = (lv_obj_t *)lv_event_get_user_data(e); // Get the passed header object

  lv_coord_t visible_width = lv_obj_get_width(cont);
  lv_coord_t scroll_x = lv_obj_get_scroll_x(cont);
  uint8_t page_index = (scroll_x + (visible_width / 2)) / visible_width;

  // Ensure we don't exceed total items
  uint8_t current_page = clampu8(page_index, 0, total_items - 1);

  if (LVGL_lock(-1)) {
    for (uint8_t i = 0; i < total_items; i++) {
      // get scroll indicator
      lv_obj_t *indicator = lv_obj_get_child(indicator_target, i);
      lv_obj_set_style_bg_opa(indicator, i == current_page ? 255 : 100, LV_PART_MAIN);
    }
    LVGL_unlock();
  }
}

void resize_footer_buttons(lv_obj_t *footer) {
  if (footer == NULL) {
    return;
  }

  // Get the number of buttons in the footer
  uint32_t button_count = lv_obj_get_child_cnt(footer);
  if (button_count == 0) {
    return;
  }

  // Calculate width for each button
  lv_coord_t button_width = lv_obj_get_width(footer) / button_count;

  uint32_t final_button_count = 0;
  // Resize each button
  for (uint32_t i = 0; i < button_count; i++) {
    lv_obj_t *button = lv_obj_get_child(footer, i);
    if (button != NULL && lv_obj_check_type(button, &lv_button_class) &&
        lv_obj_has_flag(button, LV_OBJ_FLAG_HIDDEN) == false) {
      final_button_count++;
    }
  }

  // If no buttons are visible, return early
  if (final_button_count == 0) {
    return;
  }

  uint32_t butt_width = 80 * SCALE_FACTOR;

  switch (final_button_count) {
  case 1:
    button_width = 80 * SCALE_FACTOR;
    break;
  case 2:
    button_width = 60 * SCALE_FACTOR; // Add some space between buttons
    break;
  case 3:
    button_width = 40 * SCALE_FACTOR; // Add more space for three buttons
    break;
  default:
    button_width = lv_obj_get_width(footer) / final_button_count; // Default to equal width
    break;
  }

  // Resize each button again, now that we know the final count
  for (uint32_t i = 0; i < button_count; i++) {
    lv_obj_t *button = lv_obj_get_child(footer, i);
    if (button != NULL && lv_obj_check_type(button, &lv_button_class) &&
        lv_obj_has_flag(button, LV_OBJ_FLAG_HIDDEN) == false) {
      lv_obj_set_width(button, button_width);
    }
  }
}