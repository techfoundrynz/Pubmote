# Wi-Fi menu and keyboard

Open **Menu > Wi-Fi** to scan nearby networks, reconnect to the saved network,
forget its credentials, or enter a hidden SSID using **Other network**. One
network is remembered; the firmware tool and updater share these credentials.
Open networks connect without a password.

Scanning or connecting pauses the board radio. Leaving an active Wi-Fi session
offers a restart to restore the configured board connection. Opening the page
or editing text alone does not interrupt board communication.

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
