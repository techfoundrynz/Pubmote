# Radio, diagnostic, and keyboard host tests

Run `python -m unittest discover -s tests -p test_radio.py` with CMake and a
native C11/C++17 compiler installed (Visual Studio Build Tools on Windows, GCC or
Clang elsewhere). CMake can also be found in PlatformIO's package directory.

The harness stages selected production BLE/Wi-Fi functions unchanged and
executes them against deterministic fakes. It covers interrupted fragmented
writes, overlapping sends, stale discovery completion, failed host shutdown,
scan cancellation/allocation/API failures, join cancellation (including a
simultaneous IP event), full-length SSID/key copying, and diagnostic retention/overflow.
The diagnostic capture implementation is compiled in full.

The production keypad editor is compiled directly. Its tests cover multi-tap
expiry, separate letter/digit/symbol modes, case, full buffers, UTF-8 deletion/truncation, and
independent editor instances.

These tests complement firmware builds; they do not measure radio latency or
replace on-device testing of repeated BLE → Wi-Fi → reboot transitions.
