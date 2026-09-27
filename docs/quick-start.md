# Getting started with Pubmote

Use this guide to flash your remote, calibrate its joystick, and pair it with your board.
You can also [read this guide on pubmote.com](https://pubmote.com/getting-started/).

## Before you start

Have your assembled Pubmote, a USB data cable, your board and VESC Express receiver,
and VESC Tool ready. See the [hardware prerequisites](../README.md#hardware-prerequisites)
and [Leaf Blaster example build](builds/leaf-blaster.md) if you are still assembling a remote.

## Flash firmware

1. Connect the remote to your computer with a USB data cable.
2. Open the [firmware tool](https://pubmote.com/) in a browser that supports Web Serial, such as desktop Chrome or Edge.
3. Choose **Connect Device** and select the remote's serial port.
4. Select the firmware for your exact hardware model and display variant, then flash it. Keep USB connected until the tool reports completion.
5. Restart the remote if needed. On an uncalibrated remote, the welcome screen shows a QR code for this guide. Choose **Calibrate** to begin or **Later** to dismiss it for this boot.

You can reopen the QR code from **Menu > About > Getting started** at any time.
The welcome screen stops appearing after you save joystick calibration.

## Calibrate and configure the remote

Swipe down from the top of the main screen to open the menu.
Choose **Calibration**, then **Start**. Complete these steps, using **Next** to advance:

1. **Move stick to center:** release the joystick and let it rest.
2. **Move stick to min/max:** move smoothly to the limits in all directions.
3. **Move stick within deadband:** move slightly around the center to set the region treated as no input.
4. **Set expo factor:** leave it at 1.00 for a linear response, or adjust the response curve.
5. **Axis options:** use **Invert X** or **Invert Y**, where available, to reverse an axis.
6. Check the live input display, then choose **Save** to store calibration.

Open **Settings**, swipe through the pages, and choose **Save** when finished.
The images below show an earlier UI; some labels and layouts may differ.

![Joystick calibration walkthrough](configure_mote_calibration.gif)

![Remote settings walkthrough](configure_pubmote_settings.gif)

## Prepare the receiver

These steps describe the VESC Express and Float Accessories setup.

1. Download Float Accessories from the package section of the [firmware tool](https://pubmote.com/).
2. In VESC Tool, connect to your **VESC Express**. Open **VESC Packages > Load Custom** on desktop, or **Package Store > ... > Install from file** on mobile, and install the `.vescpkg` file (unzip the download first if necessary).
3. For ESP-NOW, set **VESC Express > WiFi > WiFi Mode** to **Access Point**. Station mode can interfere with the remote connection.
4. Set **VESC Express > Bluetooth > Bluetooth Mode** to **Enabled**, or **Enabled with Scripting** if your setup needs it.
5. Under **App UI > Settings**, enable **Pubmote Enabled**. Save and restart as required.

![VESC Express WiFi configuration](configure_ve_wifi.png)

![VESC Express Bluetooth configuration](configure_ve_bluetooth.png)

![Float Accessories Pubmote setting](configure_ve_fa_settings.png)

## Pair the remote

1. Open **Menu > Pairing** on the remote to view **Paired Boards**.
2. Choose **Pair New**, then select **ESP-NOW** or **BLE** to match your receiver setup.
3. For BLE, select your receiver in the discovered-device list. If radio initialization fails, choose **Retry**.
4. In VESC Tool, open **App UI > Config > Pair Pubmote** on the receiver.
5. Check the pairing code on the remote against the receiver's pairing prompt and complete confirmation there.
6. Once paired, return to the main screen and check that board telemetry appears. Use the **Paired Boards** list to select a saved board later.

![Float Accessories pairing](configure_ve_fa_pairing.png)

![Remote pairing walkthrough (earlier UI)](configure_mote_pairing.gif)

## Enable remote input on the board

Pairing and board-side input configuration are separate steps. For Refloat, open
**Refloat Cfg > Remote** in VESC Tool while connected to the **VESC controller**:

- Set **Remote Type** to **UART**.
- Set **Tiltback Angle Limit** above zero to allow a tilt adjustment.
- Set **Tiltback Speed** above zero to allow the angle to change.
- Keep **Input Deadband** below 100% and at least 1%.
- Leave **Throttle Current Maximum** at **0** unless you intentionally configure remote throttle.

Save the board configuration. With the board stationary in a controlled setting,
check that the joystick centers correctly and that the input direction and response
match your settings before riding.

![Refloat remote input configuration](configure_vesc_refloat.png)

## Everyday use

While the main screen is active, the joystick sends remote input to the board.
The board's configuration determines the resulting tilt or throttle response.
Swipe down to open the menu. Use **About > Check for updates** to access firmware
updates, and **About > Getting started** to reopen this guide's QR code.

## Troubleshooting

### The remote will not connect or stay connected

Check that the intended board is selected in **Paired Boards**, that both devices
use the same wireless protocol, and that the receiver is powered. For ESP-NOW,
check that VESC Express WiFi mode is **Access Point**, rather than Station.

### Pairing stays on dashes or 0000

Check that Float Accessories is running and **Pubmote Enabled** is checked.
Start **Pair Pubmote** on the receiver as well as pairing on the remote. The
LispBM Scripting tab in VESC Tool can show package errors. Check firmware/package
compatibility if the handshake never starts.

### Connected, but board tilt does not change

Recheck the Refloat remote settings above, particularly **UART**, nonzero angle
and speed limits, and input deadband. Confirm that Float Accessories communicates
with the controller over CAN; pairing to the receiver alone does not establish
that connection.

### Input direction is reversed or the joystick drifts

Repeat calibration. Use **Invert Y** (or **Invert X**, if available) to change
direction. Release the stick during the center step and set enough deadband to
cover small movements around center. Remember to **Save**.

### Board battery always shows 0%

Older board packages may not provide compatible battery telemetry. Check your
Refloat/Float package version and its telemetry support before treating this as a
remote battery problem.

### Still stuck?

Include your remote hardware, firmware version and hash from **About**, receiver
and controller firmware versions, wireless protocol, and the step that fails when
[opening an issue](https://github.com/techfoundrynz/Pubmote/issues)
