# Charging screen and charger wake

Connecting USB power shows the charge screen once per connection, including booting with USB already connected. Tap anywhere to return to the previous Stats or Menu screen. Unplugging also returns to that screen. Dismissal lasts until the next unplug/replug.

The screen shows battery percentage and Charging, Charged, or Power connected, using the charger status rather than treating a full battery as disconnected. Settings, calibration, games, pairing, Wi-Fi, update operations, and confirmation dialogs defer presentation until the UI returns to Splash, Stats, or Menu. On Stats, a board connection (connecting, connected, or reconnecting) also defers presentation: automatically leaving Stats would release control forwarding. The charge screen remains pending until you disconnect or leave Stats. Unplugging cancels a deferred presentation.

The power task publishes a thread-safe connection snapshot. The UI checks it every 100 ms; the power task detects changes through the PMU interrupt, with a one-second polling fallback. All charge-screen navigation and property updates stay on the UI task.

On Pingumote, SY6970 PG_STAT (register 0x0B, bit 2) detects valid charger input independently of ADC conversions and DPDM source classification. Its active-low interrupt on GPIO14 is armed as EXT0 before deep sleep; the configured power button remains EXT1. Shutdown disables charger ADC/watchdog first, waits for the interrupt to settle, and arms charger wake only when the pin is inactive. The pin returns to digital GPIO mode at the start of the next boot. Only a disconnected-to-connected power change accepts a PMU wake; detach, charging-complete, and fault interrupts return to sleep.

Hardware validation:

1. Plug in while on Stats or Menu: charge screen appears and percentage updates.
2. Tap to dismiss: normal screen returns and charging continues without reopening.
3. Unplug/replug: charge screen appears again.
4. Shut down on battery, then plug in: boot without holding the power button, directly to the charge screen.
5. Shut down while connected: remain asleep; unplug should not leave the screen on; reconnect should wake.
6. Confirm the normal long-press power-button wake still works.
7. While Stats controls a connected board, plug in: remain on Stats with controls forwarded. Disconnect or open Menu: the pending charge screen appears. Unplugging before then cancels it.

References: [SY6970 manufacturer datasheet](https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series/blob/master/datasheet/SY6970%20Datasheet.pdf), [Espressif ESP32-S3 sleep modes](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html).
