# Radio, diagnostic, and keyboard host tests

Run `python -m unittest discover -s tests -p test_radio.py` with CMake and a
native C11/C++17 compiler installed (Visual Studio Build Tools on Windows, GCC or
Clang elsewhere). CMake can also be found in PlatformIO's package directory.

The harness stages selected production BLE/Wi-Fi and radio-session functions unchanged and
executes them against deterministic fakes. It covers interrupted fragmented
writes, overlapping sends, stale discovery completion, failed host shutdown,
scan cancellation/allocation/API failures, join cancellation (including a
simultaneous IP event), full-length SSID/key copying, and diagnostic retention/overflow.
The diagnostic capture implementation is compiled in full.

The production keypad editor is compiled directly. Its tests cover multi-tap
expiry, separate letter/digit/symbol modes, case, full buffers, UTF-8 deletion/truncation, and
independent editor instances.

These tests complement firmware builds; they do not measure radio latency or
replace on-device testing of repeated BLE/ESP-NOW to Wi-Fi and back transitions.

Radio-session tests verify worker shutdown before handoff, restoration before
peer connection, preserved transport and disconnect intent, failed preparation,
retained Wi-Fi driver cleanup, failed worker startup, and retry after failed
restoration. They also assert that live IP Wi-Fi prevents board-radio startup.

ESP-NOW tests cover failed deinitialization and Wi-Fi teardown, rejection of
initialization while teardown is incomplete, cleanup/restart retries, and IP
handoff after a failed teardown. OTA tests cover cancellation before and during
downloads (including the final read), commit/cancellation arbitration, bounded
read timeouts, redirects and HTTPS downgrade rejection, truncated/oversized
responses, image/write failures, and cleanup without boot selection on failure.

The processing-overlay harness compiles the complete production UI operation
runner against fake timers, UI events, and worker requests. It verifies that
feedback appears before work starts, duplicate operations are rejected, work and
completion run on their respective threads, failures leave a retry path, and
quick operations/restarts wait for the 500 ms minimum. Work lasting longer than
500 ms completes without an added delay.
