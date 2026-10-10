# Telemetry ownership

Owner: telemetry maintainers, with receiver/power and UI review for publication or
observer changes. `remote/stats.h` exposes copied readings, grouped publications
and observer registration. `config` owns plain board telemetry value types.

Board and remote-power publishers update only their own field groups under a
short critical section. A snapshot includes the 64-bit timestamp and is copied
by value; callers retain no pointers into internal state. Board data uses KPH,
Celsius and metres; the application authenticates packets and supplies time.

`stats_update()` copies the observer list and invokes callbacks outside the lock
on the calling task. Registration returns false at the fixed capacity of 16;
duplicate registration succeeds without adding another callback. An observer
removed during dispatch can finish that notification, so callback functions need
static lifetime. UI observers must dispatch copied values to the event loop.

Validation: native state tests cover concurrent grouped publication/snapshots,
capacity and reentrant observers; command tests cover authenticated publication
and rejected-packet side effects. Run the component boundary checker and board
builds when changing public headers or publication interfaces.
