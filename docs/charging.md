# Charging screen and charger wake

Connecting USB power after boot shows the charge screen once per connection. A reset (including after flashing) or power-button wake with USB already connected starts normally; an actual charger wake from deep sleep opens the charge screen. Tap anywhere to return to the previous Stats or Menu screen. Unplugging normally also returns to that screen. Dismissal lasts until the next unplug/replug.

If charger detection woke the remote from deep sleep, unplugging while its charge screen remains undismissed powers the remote off again. Tapping to dismiss takes over normal operation, so unplugging then leaves it on, including later charging sessions during the same boot. A power-button wake or reset with USB already connected does not enable this automatic shutdown.

The screen shows battery percentage and Charging, Charged, or Power connected, using the charger status rather than treating a full battery as disconnected. Settings, calibration, games, pairing, Wi-Fi, update operations, and confirmation dialogs defer presentation until the UI returns to Splash, Stats, or Menu. On Stats, a board connection (connecting, connected, or reconnecting) also defers presentation: automatically leaving Stats would release control forwarding. The charge screen remains pending until you disconnect or leave Stats. Unplugging cancels a deferred presentation.

The power task publishes a thread-safe connection snapshot. The UI checks it every 100 ms; the power task detects changes through the PMU interrupt, with a one-second polling fallback. All charge-screen navigation and property updates stay on the UI task.

On Pingumote, SY6970 PG_STAT (register 0x0B, bit 2) detects valid charger input independently of ADC conversions and DPDM source classification. Its active-low interrupt on GPIO14 is armed as EXT0 before deep sleep; the configured power button remains EXT1. Shutdown disables charger ADC/watchdog first, waits for the interrupt to settle, and arms charger wake only when the pin is inactive. The pin returns to digital GPIO mode at the start of the next boot. Only a disconnected-to-connected power change accepts a PMU wake; detach, charging-complete, and fault interrupts return to sleep.

Wake sources are read as a bitmap. If the power button and PMU wake together, the normal long-press button check takes priority and the boot is treated as a button wake.

Hardware validation:

1. Plug in while on Stats or Menu: charge screen appears and percentage updates.
2. Tap to dismiss: normal screen returns and charging continues without reopening.
3. Unplug/replug: charge screen appears again.
4. Shut down on battery, then plug in: boot without holding the power button, directly to the charge screen.
5. Shut down while connected: remain asleep; unplug should not leave the screen on; reconnect should wake.
6. Confirm the normal long-press power-button wake still works.
7. While Stats controls a connected board, plug in: remain on Stats with controls forwarded. Disconnect or open Menu: the pending charge screen appears. Unplugging before then cancels it.
8. Wake by plugging in, leave the charge screen undismissed, then unplug: power off. Repeat but tap to dismiss first: remain on. Power on with the button while charging and unplug without dismissing: remain on.
9. Flash or reset with USB connected: start normally without the charge screen. Unplug/replug afterwards: the charge screen appears.

References: [SY6970 manufacturer datasheet](https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series/blob/master/datasheet/SY6970%20Datasheet.pdf), [Espressif ESP32-S3 sleep modes](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html).
