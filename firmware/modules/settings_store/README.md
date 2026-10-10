# Settings persistence

Owns typed NVS reads, writes, commits, and reset for the existing `nvs` namespace.
It does not read runtime settings or apply device preferences. Callers provide
values from snapshots and handle errors before applying changes. Boot recovery
and settings migrations remain in application composition. Keys, blob layouts,
and commit behavior are retained.
