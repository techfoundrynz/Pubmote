# Configuration and shared value types

Ownership: board configuration and persisted schema maintainers.

Public API: `config.h`, `colors.h`, and `remote/settings_types.h`. This header-only
component owns build-time board constants and plain persisted value types. It
does not own live settings, storage, peripherals, or tasks.

Dependencies: none. Keep Slint, hardware driver types, and live service APIs out
of these headers. Persisted enum values and structure layouts require migration
review; do not renumber existing entries.

Validation: settings host tests and a firmware build for each affected board.
