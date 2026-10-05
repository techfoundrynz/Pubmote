# Wi-Fi menu and keyboard

Open **Menu > Wi-Fi** to scan nearby networks, reconnect to the saved network,
forget its credentials, or enter a hidden SSID using **Other network**. One
network is remembered; the firmware tool and updater share these credentials.
Open networks connect without a password.

Scanning or connecting pauses the board radio. Back stops Wi-Fi, restores the
configured BLE or ESP-NOW transport and board workers, then reconnects if the
board was connected or connecting before the session. An explicitly disconnected
board stays disconnected. Opening the page or editing text alone does not
interrupt board communication. Failed restoration stays on the page; Back retries.

If the updater has no saved network, **Set up Wi-Fi** opens this page directly.
Back then returns to the updater using the newly saved credentials. Leaving the
updater also restores board communication without rebooting; installing firmware
still requires the updater's **Reboot** action.

Radio preparation and restoration show the shared animated processing overlay.
It stays visible for at least 500 ms and blocks repeated taps until completion.
Board/protocol changes, pairing restoration, reset/restart/shutdown, settings
and calibration saves, sensor calibration, and game storage operations use the
same feedback. Background work uses the existing internal radio worker stack;
Slint updates and rendering stay on the UI thread. Game Lua initialization is
still bounded to 250 ms on that thread, after package loading has finished.

The reusable keyboard uses the standard page header, with typed text in its
subtitle. Long input keeps its latest characters visible. Passwords start
masked, with **Show / Hide** available.

- Tap a letter group again within one second to cycle characters.
- Use **abc / ABC** for case and **123 / #+= / abc** for input mode.
- Letters, digits, and symbols each have their own mode; letter and symbol keys
  do not include digit shortcuts.
- Hold **Del** to delete repeatedly; **Done** accepts input and the header's
  back chevron discards the editor.

SSIDs allow 32 UTF-8 bytes. Passwords allow 8–63 bytes, a 64-digit hex key,
or an empty value for an open network. The keypad enters ASCII and preserves
existing UTF-8 text; deletion removes whole UTF-8 characters.

`firmware/src/slint/ui/keyboard.slint` and `utilities/keypad.h` are independent
of Wi-Fi and can be reused by other screens. Host tests are described in
[tests/radio](../tests/radio/README.md). Touch layout and BLE/ESP-NOW handoff
still need on-device verification.
