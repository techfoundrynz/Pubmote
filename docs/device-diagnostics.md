# Device diagnostics

Connect the remote at pubmote.com and select **Download device diagnostics** in
Device Information. The JSON file contains the build ID, hardware, uptime,
internal/PSRAM memory metrics, and recent BLE, Wi-Fi, connection, and OTA logs.
Firmware without this command reports that an upgrade is needed.

The same report is available from the USB console:

```text
diagnostics
diagnostics clear
```

Capture retains up to 16 KB in PSRAM and disappears on reboot. It records only
selected connectivity tags; console and settings output are excluded. Network
names and device addresses can appear in connectivity messages. Clearing the
capture resets its record-loss counters as well as its contents.

- `overwritten`: complete old records removed to make room for new ones.
- `truncated`: records shortened to the per-record limit, with a marker.
- `dropped`: records skipped because the capture lock was busy. Capture never
  waits for that lock on a radio task.

If capture cannot allocate PSRAM, memory metrics remain available and
`capture_available` is false. Capture does not reserve scarce internal RAM as a
fallback. The console's normal log output is preserved.

The host failure tests are documented in [tests/radio](../tests/radio/README.md).
On-device verification should include repeated BLE connections/disconnections,
entry to the updater, and recovery by reboot, while checking control/telemetry
and exported diagnostic counters.
