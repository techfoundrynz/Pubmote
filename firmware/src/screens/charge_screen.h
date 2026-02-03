#ifndef __ABOUT_SCREEN_H
#define __ABOUT_SCREEN_H

bool is_charge_screen_active();

void charge_screen_load_start(lv_event_t *e);
void charge_screen_loaded(lv_event_t *e);
void charge_screen_unload_start(lv_event_t *e);

#endif