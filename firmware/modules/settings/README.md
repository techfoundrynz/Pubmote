# Settings ownership

Owner: settings/configuration maintainers; consumers review schema changes.
`remote/settings_snapshot.h` exposes copied reads. `remote/settings_state.h`
exposes boot initialization, validated masked preferences and explicit record or
paired-device updates. Settings value types belong to `config`.

Records are private. A full snapshot is copied under one short critical section;
separate getters do not form a combined snapshot. Device field masks avoid losing
unrelated concurrent edits; pins/calibration and peer address/channel publish
together. Rejected updates leave records unchanged. Boot initialization precedes
worker startup. Callers retain ownership of arguments; no pointers are stored.

Critical sections only copy/mutate memory. Flash, callbacks, radio lifecycle,
worker waits and UI effects belong to application workflows and run outside them.
This API does not serialize whole settings workflows or make NVS transactional.

Validation: native state tests link the production code with a real mutex and
exercise snapshots, concurrent masked updates, validation and pairing operations;
application settings and pairing suites cover the caller contracts. Run the
component boundary checker and all board builds for public-header changes.
