# Firmware architecture and ownership

The long-term design is ESP-IDF components organized by responsibility, with
private state and documented task/lifetime contracts. Keep portable rules apart
from hardware and Slint where that provides test or simulator reuse. Introduce
request queues only for services that need task ownership; do not add tasks or
an event bus simply to make a component boundary.

## Current build boundaries

`firmware/modules/` contains first-party components. Their implementations cannot
include application headers or another component's private sources. Public
dependencies belong in `REQUIRES`; implementation-only dependencies belong in
`PRIV_REQUIRES`. Folder names are also their ESP-IDF component names.
Public headers live under `include/`, implementations under `src/`, and include
spellings such as `remote/comms.h` remain stable. Vendored code stays in
`firmware/components/`. The dependency graph is acyclic:

- `config`: board constants and plain settings value types.
- `support`: infrastructure, diagnostics, framing and utility functions;
  depends on configuration.
- `transport`: BLE/ESP-NOW implementations; uses support privately.
- `ota`: network download and flash update implementation.
- `game_store`: filesystem-backed game storage.
- `settings`: private settings records, copied snapshots, validated preference
  updates and atomic paired-device operations.
- `telemetry`: private readings, grouped publications, copied snapshots and observers.
- `protocol`: portable command identifiers and wire codecs; depends only on plain
  configuration value types, with no transport, UI or scheduler dependency.
- `settings_store`: typed NVS persistence; accepts values supplied by callers
  and does not apply runtime preferences or call hardware.
- Application (`firmware/src`): boot, device services, lifecycle coordination,
  hardware integration and Slint presentation; consumes those components.

See the [application contracts](../firmware/src/README.md) for service and UI
coordination rules, and each module's README for its public API and validation.

Application headers are private to its component. Application sources are
registered explicitly in `firmware/src/sources.cmake`; the boundary check rejects
unregistered files. The PlatformIO prebuild hook fingerprints project CMake files,
component manifests and CMake arguments per board. A changed fingerprint clears
that board's CMake cache before configuration, preventing deleted/moved sources
from surviving in its cached build graph; unchanged definitions keep the cache.
`remote/display.h` is the C display-control interface;
`ui/slint_window.h` is an explicit UI-only dependency.
Connection/pairing state is private to `connection.c`; use its atomic getter and
update APIs. Separate getter calls are not a combined status snapshot, and
atomic state access does not serialize lifecycle transitions or their side
effects. Lifecycle work must retain existing worker sequencing.

## Runtime ownership

| Resource/state | Owner | Access contract |
| --- | --- | --- |
| BLE/ESP-NOW driver lifetime | Transport, coordinated by radio-session worker | Stop connection/RX/TX workers before IP handoff; respect teardown errors |
| Link/pairing state | Connection service | API reads; connection logic updates link state; pairing protocol/screen and settings deletion update pairing state |
| Preferences, calibration, pins and paired-device data | Settings, coordinated by application settings | Copied reads; masked preference updates; pins/calibration and address/channel published together; transport uses boot-bound channel callbacks |
| Telemetry | Telemetry component | Board packet and remote power groups published atomically; UI frames capture snapshots by value |
| Settings persistence/migrations | Settings store/application boot | NVS runs outside state locks; existing keys, blob layouts and per-key error behavior retained |
| Slint window/models | UI event loop | Workers dispatch copied values through `slint::invoke_from_event_loop` |
| Panel IO/DMA buffers | Display integration | Preserve panel mutex and buffer ownership until DMA completion |
| OTA request and cancellation/commit gate | Update operation in application | Client runs on maintenance worker; caller retains context until completion |
| Game files | Game store | Store serializes filesystem operations; caller frees returned source |
| Diagnostic ring | Support diagnostic capture | Capture owns mutex/ring; snapshots copy retained data |

`settings_snapshot()` copies all settings records under one short critical
section; narrower getters copy a single record. Separate getter calls are not a
combined snapshot. Device updates use a field mask so changing brightness does
not overwrite a concurrent theme update. Calibration and IMU previews publish
whole records; their application workflows must retain existing sequencing.
Boot loads local defaults/NVS records and publishes them before tasks start.

`stats_snapshot()` copies telemetry, including the 64-bit timestamp. RX publishes
one authenticated packet at a time; power publishes its four fields together.
Each publisher changes only its own field group. `stats_update()` copies the
observer list and invokes it outside the lock on the calling task. A callback
removed during dispatch can finish that notification; callback functions must
have static lifetime. UI callbacks capture the reading used to format the frame.

Locks protect in-memory copies and updates only. They do not make multi-key NVS
writes transactional, serialize radio lifecycle work, or serialize all settings
workflows. Flash, worker waits, radio calls and display effects run outside them.
The settings console retains successful earlier changes if a later write fails.
Wrong-secret board-data packets do not publish board readings or refresh
sleep/connection timeouts. Link RSSI still comes from transport reception.

## Reviewer/process ownership

Each component README identifies the responsible area, public API, runtime
contract, and checks. PRs identify the affected modules and interfaces. Interface
changes require review from both the provider and consumer areas. Configuration
schema and protocol changes also involve firmware-tool/receiver compatibility
review. Reviewer account assignments are outside this refactor. Required
reviewer/CI branch protection is a repository setting, not enforced by these files.

Run `python scripts/check_firmware_boundaries.py`. CI runs this independently of
firmware builds. The checker covers first-party header dependencies, component
cycles, source registration, state encapsulation, and application UI boundaries.
Firmware builds validate SDK dependencies.
New components need explicit source lists, a README, private
implementation files, declared dependencies, and focused validation. Keep SDK
or third-party code in `firmware/components/` out of first-party ownership checks.

## Application boundaries

The application coordinates services whose lifecycle depends on boot and hardware.
Its internal boundaries keep presentation out of packet handling and panel setup:

- `remote/pairing.c` owns pairing actions and lifecycle calls. It uses the pure
  protocol codecs and the C `ui/pairing_feedback.h` adapter; it cannot access Slint.
- `remote/commands.c`, receiver and transmitter apply authentication, timestamps,
  publication and transport effects around `modules/protocol` codecs. Wire layout
  and legacy version-length rules live in the codec, not those workers.
- `display/panel.cpp` owns panel/touch handles, SDK initialization, brightness,
  sleep and the recursive panel IO lock. It cannot depend on presentation.
- `display/slint_display.cpp` owns the Slint platform, window, rendering buffers,
  DMA completion semaphore and event/input tasks. Screen setup and teardown are
  delegated to `ui/navigation.cpp`, which also owns cached navigation state.
- `ui/device_preferences.cpp` presents preferences and applies display effects;
  `ui/color_utils.*` contains Slint image/color helpers. Workers use C adapters,
  including `ui/screen_status.h` for cached screen queries, rather than screen headers.

Read-only consumers include `remote/settings_snapshot.h`; writers explicitly
include `remote/settings_state.h`. Application settings and typed persistence
headers are separate dependencies. Shared input and board telemetry value types
live in `config` without ADC, FreeRTOS, UI or storage headers.

The boundary checker enforces worker/UI, panel/presentation and renderer/navigation
separation as well as component dependencies. Keep task priorities, polling periods,
DMA ownership and shutdown ordering intact when extending these interfaces.
Future service extraction should follow downward dependencies and a demonstrated
need for task ownership; the current split does not require additional tasks.

## Validation

- Component boundary check and existing host tests (settings, sleep, radio, OTA,
  diagnostics, UI operations, keypad, DMA and game/tool compatibility).
- Native state tests exercise concurrent packet/power publishers and readers,
  paired-device operations, masked updates, rejected values and reentrant
  observers. Packet tests cover scaling, authenticated publication and rejected
  packet side effects. Portable codec tests exercise golden wire bytes, malformed
  lengths, secrets, channel rules and legacy scaling. Pairing tests link the real
  state and codec implementations with transport/UI adapters stubbed. These run with the existing CMake host suite in CI.
- Firmware builds across LCD and AMOLED configurations for include/driver changes.
- Hardware checks for input/transmit timing, radio handoff/recovery, OTA
  cancellation/recovery, panel rendering, and internal/PSRAM headroom.

Host tests/builds cannot establish unchanged real-time latency, power use, or
electrical/panel behaviour. Keep existing on-device diagnostics and benchmarks.
