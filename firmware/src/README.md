# Application composition and coordination

Ownership: firmware application maintainers, with connectivity, hardware and UI
review for their respective service interfaces.

This component owns boot order, device services, radio-session coordination,
power/shutdown policy, screen presentation and Slint integration. Reusable
transport, OTA, storage, configuration and support code lives in
[ESP-IDF modules](../../docs/firmware-architecture.md#current-build-boundaries). Application headers are not exported to
those components. Register handwritten sources in `sources.cmake`; generated
Slint sources are registered separately in the build directory.

`remote/connection.h` exposes atomic state reads and explicit updates, not mutable
globals. `remote/display.h` exposes hardware/control functions;
`ui/slint_window.h` is UI-only. Worker callbacks must copy retained input and
dispatch window/model changes to the Slint event loop. Preserve radio-session
worker sequencing and the existing panel lock/DMA completion contracts.

Settings and telemetry records belong to `modules/settings` and
`modules/telemetry`; use copied snapshots
and explicit updates. Application settings owns boot loading, migrations,
persistence workflows and hardware effects; typed NVS mechanics belong to
`modules/settings_store`. See [architecture and ownership](../../docs/firmware-architecture.md)
for task contracts and the application boundaries.

Validation: component boundary checker, relevant host and firmware-tool tests,
LCD/AMOLED firmware builds, and hardware timing/recovery checks for runtime changes.

Pairing is C service logic with a `ui/pairing_feedback.h` adapter. Packet layout
belongs to `modules/protocol`. Display responsibilities are split between
`display/panel.cpp` (hardware), `display/slint_display.cpp` (platform/tasks/DMA),
`ui/navigation.cpp` (screen lifecycle) and `ui/device_preferences.cpp` (presentation).
Workers query cached screen state through `ui/screen_status.h`.
