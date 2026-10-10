#pragma once
// Called by the Slint platform owner, on its event-loop task except prepare_shutdown.
void ui_navigation_init(int reset_reason);
void ui_navigation_prepare_shutdown(void);
void ui_navigation_shutdown(void);
