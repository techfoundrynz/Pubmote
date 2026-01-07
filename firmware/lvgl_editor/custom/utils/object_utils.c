#include "object_utils.h"
#include <string.h>

lv_obj_t *find_child_by_name_recursive(lv_obj_t *parent, const char *name) {
  uint32_t child_cnt = lv_obj_get_child_count(parent);

  for (uint32_t i = 0; i < child_cnt; i++) {
    lv_obj_t *child = lv_obj_get_child(parent, i);

    // Check if this child has the matching name
    const char *child_name = lv_obj_get_name(child);
    if (child_name && strcmp(child_name, name) == 0) {
      return child;
    }

    // Recursively search in this child's children
    lv_obj_t *found = find_child_by_name_recursive(child, name);
    if (found)
      return found;
  }

  return NULL;
}