# Shared support

Ownership: firmware infrastructure maintainers.

Public API: `utilities/*`. Owns conversion/formatting helpers, VESC framing,
callback registries, diagnostic log capture, memory diagnostics, and allocation
helpers. UI operations remain in the application; this component cannot depend
on Slint or device services.

Runtime: diagnostic capture owns its mutex and PSRAM ring; snapshots copy data
for the caller, who releases them with `diagnostic_log_snapshot_free`. Callback
registries invoke callbacks synchronously on the caller
task and do not provide synchronization. Their users must serialize registration
and invocation. `create_psram_task` requires the caller to own the static task
storage and clean up only after task completion. Keep the retained stack across
restarts; PSRAM-backed tasks must not write flash/NVS or enter deep sleep.
The internal-RAM reserve wrapper
runs during SDK startup; its link option belongs to this component.

Validation: diagnostic and keypad host tests, firmware build, and memory checks
on hardware when changing allocation policy.
