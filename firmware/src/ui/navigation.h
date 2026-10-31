#pragma once
// Called by the Slint platform owner, on its event-loop task except prepare_shutdown.
void ui_navigation_init(int reset_reason);
void ui_navigation_prepare_shutdown(void);
void ui_navigation_shutdown(void);

// Dispatch device readiness to the UI task; the splash minimum is enforced there.
#ifdef __cplusplus
extern "C"
#endif
    void ui_navigation_finish_boot(void);
