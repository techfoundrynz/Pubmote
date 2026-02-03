#ifndef __PAIRING_SCREEN_H
#define __PAIRING_SCREEN_H

bool is_pairing_screen_active();

void pairing_screen_load_start(lv_event_t *e);
void pairing_screen_loaded(lv_event_t *e);
void pairing_screen_unload_start(lv_event_t *e);

#endif