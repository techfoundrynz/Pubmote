# Charging screen and charger wake

The normal one-second boot splash is shown before entering charging after a charger wake (tapping can skip the splash). The charge page uses the saved display brightness. After 30 seconds it sets brightness to zero and disables high brightness mode, leaving touch active. The first tap while dark restores the saved display settings and starts another 30-second window; it stays on the charge page and does not dismiss a charger-only wake. Tapping while visible dismisses the page. Leaving the page always restores saved display settings. These temporary overrides do not change saved settings.

On devices with an IMU, significant movement while the charge display is dark also restores the saved brightness and starts another 30-second window without dismissing the page or changing charger-only shutdown behavior. Movement detection uses a filtered acceleration change of 0.2 g over two samples, independent of orientation; resting face-up, small vibration, invalid readings, and isolated single-sample spikes do not wake it. Events queued before blanking or from an earlier charge-screen visit are ignored. Movement while visible does not extend the timeout.

While the charge page is open, a dedicated LED effect breathes gently in amber on a two-second cycle during charging, holds green when charged, and holds dim amber when power is connected without active charging. It continues when the display blanks. The LED Off setting stays dark. Leaving the page resumes the underlying LED mode or alert; startup timers and settings refreshes cannot replace the charging effect while the page remains open.

Connecting USB power after boot shows the charge screen once per connection. A reset (including after flashing) or power-button wake with USB already connected starts normally; an actual charger wake from deep sleep opens the charge screen. Tap anywhere while visible to return to the previous Stats or Menu screen; a tap while dark only wakes the display. Unplugging normally also returns to that screen. Dismissal lasts until the next unplug/replug.

If charger detection woke the remote from deep sleep, unplugging while its charge screen remains undismissed powers the remote off again. Tapping to dismiss takes over normal operation, so unplugging then leaves it on, including later charging sessions during the same boot. A power-button wake or reset with USB already connected does not enable this automatic shutdown.

The screen shows battery percentage and Charging, Charged, or Power connected, using the charger status rather than treating a full battery as disconnected. Settings, calibration, games, pairing, Wi-Fi, update operations, and confirmation dialogs defer presentation until the UI returns to Splash, Stats, or Menu. On Stats, a board connection (connecting, connected, or reconnecting) also defers presentation: automatically leaving Stats would release control forwarding. The charge screen remains pending until you disconnect or leave Stats. Unplugging cancels a deferred presentation.

The power task publishes a thread-safe connection snapshot. The UI checks it every 100 ms; the power task detects changes through the PMU interrupt, with a one-second polling fallback. All charge-screen navigation and property updates stay on the UI task.

The charge screen can show an approximate time remaining, rounded up to five minutes, learned from previous completed charges. It uses filtered battery voltage and charging current rather than the voltage-derived percentage. There is no battery-capacity setting. The estimate stays hidden until matching history exists and readings have settled for 15 seconds; it also hides when charging stops or telemetry is invalid. Devices without usable charging-current readings cannot learn an estimate.

Learning continues whether the charge screen is open or closed. Each uninterrupted segment ending in the charger's actual Done state updates the voltage/current ranges observed in that segment; unplugged or faulted sessions are discarded. The latest completed charge is blended into existing history, and ranges unused for more than eight learned completions stop contributing. A consistently large change across several ranges replaces the old profile, allowing it to adapt to a replacement battery. This is an approximation based on prior conditions, not a guaranteed completion time.

The learned profile is saved only after a qualifying completed charge and survives restart. Factory reset clears it and prevents pending profile saves from restoring erased history. Both Extras' Factory reset and the console `erase` command use the guarded maintenance-worker reset; reboot occurs only after a successful erase.

On Pingumote, SY6970 PG_STAT (register 0x0B, bit 2) detects valid charger input independently of ADC conversions and DPDM source classification. Its active-low interrupt on GPIO14 is armed as EXT0 before deep sleep; the configured power button remains EXT1. Shutdown disables charger ADC/watchdog first, waits for the interrupt to settle, and arms charger wake only when the pin is inactive. The pin returns to digital GPIO mode at the start of the next boot. Only a disconnected-to-connected power change accepts a PMU wake; detach, charging-complete, and fault interrupts return to sleep.

Wake sources are read as a bitmap. If the power button and PMU wake together, the normal long-press button check takes priority and the boot is treated as a button wake.

Hardware validation:

For an intermittent charging failure on SY6970 boards, open About's battery diagnostics before rebooting or unplugging. Charger and power-management logs remain available through the general `diagnostics` command. The charging-specific console command has been removed.

On About's diagnostics page, the SY6970 section shows USB power-good, input-source classification, charge enable, HIZ/OTG mode, input-current limit, input regulation, current fault, last nonzero fault seen since boot, and sample age. The UI requests a refresh and reads a cached snapshot; all register reads stay on the power task. Initially it shows `Reading...`. The last fault survives subsequent fault-register reads until reboot.

The power task automatically logs configuration and input-regulation registers when input status changes into a powered-but-not-charging state. This preserves evidence even if a later console request or restart clears the failure.

Useful registers: REG00 contains input-current limit and HIZ; REG03 contains charge enable and OTG; REG0B contains input classification, charging phase, and power-good; REG0C reports watchdog, input/thermal/timer, battery, and temperature faults; REG13 contains input-current and input-voltage regulation status. Capture these alongside the battery voltage, state, and current shown on About. A restart that restores charging indicates a recoverable state but does not identify the cause by itself.

1. Plug in while on Stats or Menu: charge screen appears and percentage updates.
2. Tap to dismiss: normal screen returns and charging continues without reopening.
3. Unplug/replug: charge screen appears again.
4. Shut down on battery, then plug in: boot without holding the power button, show the boot splash, then the charge screen.
5. Shut down while connected: remain asleep; unplug should not leave the screen on; reconnect should wake.
6. Confirm the normal long-press power-button wake still works.
7. While Stats controls a connected board, plug in: remain on Stats with controls forwarded. Disconnect or open Menu: the pending charge screen appears. Unplugging before then cancels it.
8. Wake by plugging in, leave the charge screen undismissed, then unplug: power off. Repeat but tap to dismiss first: remain on. Power on with the button while charging and unplug without dismissing: remain on.
9. Flash or reset with USB connected: start normally without the charge screen. Unplug/replug afterwards: the charge screen appears.
10. Open charging at a high and a low saved brightness: each uses the saved level, then drops to zero after 30 seconds. Tap once while dark: restore the saved brightness and remain on charging for another 30 seconds. Tap while visible or unplug: restore normal display settings and LED behavior. Repeat by unplugging while dark, including charger-only wake auto-off. Changing saved brightness while dark must not light the screen until a wake tap.
11. With LEDs enabled, verify amber breathing during charging, steady green when full, and dim amber when only power is connected. Verify the effect continues with the display blanked and past the startup timer, then resumes the configured mode on dismissal. LED Off must stay dark throughout.
12. On an IMU device, leave the remote stationary through the 30-second timeout: it stays dark. Pick it up or rotate it: restore saved brightness while remaining on charging, then blank again after 30 seconds. Unplug after a movement wake from a charger-only boot: still power off. Devices without an IMU retain tap-to-wake.

References: [SY6970 manufacturer datasheet](https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series/blob/master/datasheet/SY6970%20Datasheet.pdf), [Espressif ESP32-S3 sleep modes](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html).

## Hidden Extras menu

Tap the About version information five times within the existing tap window to open Extras. It contains Factory reset, Shipping mode (on supported boards), and Arcade. Factory reset keeps its confirmation and resets settings through the radio/session worker before restarting. Arcade returns to Extras; Extras returns to About. The main menu's Shutdown button only shuts down; its factory-reset long-press shortcut has been removed.

## Shipping mode for storage

SY6970 boards offer an **Enter shipping mode** action on the hidden Extras menu. It is available only with USB unplugged and asks for confirmation: the remote turns off by cutting its battery power path, and USB reconnect wakes it. This is a one-time action, not a saved preference or a running-state toggle. Other boards hide the action.

Pending settings saves finish before the power task shuts down peripherals and disables BATFET. USB is checked again on that task and immediately before cutoff. Failed register reads or writes do not report success; if shutdown already started, the remote restarts and shows a failure notice. Normal Shutdown still uses deep sleep and retains button/charger wake.

Hardware check: unplug USB, open Extras, cancel once to confirm the remote stays on, then confirm shipping mode. Verify system power is removed and reconnect USB to recover. Test reconnecting USB during confirmation and during shutdown: shipping must be refused or report failure. Confirm saved settings survive. Storage current and board-specific button recovery still need physical measurement.
