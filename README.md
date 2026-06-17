[![Publish Release](https://github.com/techfoundrynz/PubRemote/actions/workflows/release.yml/badge.svg)](https://github.com/techfoundrynz/PubRemote/actions/workflows/release.yml)

# Pubmote

Welcome to Pubmote!

Pubmote is a feature-rich, ESP-NOW based remote control for VESC based onewheels. Pubmote connects to a VESC Express receiver running the Float Accessories package, which provides a feature-rich platform and easy configuration experience.

> [!TIP]
> Want an easy way to update firmware or flash a new device? Head over to the firmware tool at [https://pubmote.com](https://pubmote.com)

<img width="787" height="721" alt="image" src="https://github.com/user-attachments/assets/419fa512-16eb-4ff9-93be-6c0b65ef92af" />


Suggestions for additional hardware targets are welcomed. For new projects, we recommend using a custom PCB or dev-board the following features:

- Buzzer
- PMU with I2C comms. SY6970 and AXP2101 are currently supported.
- GPIO with RTC supported pins

## Getting started

### Hardware Prerequisites

#### ESP32S3-based controller with a display, and a case.  

**Current implementations:** 
- [WaveShare 1.43in Amoled display](https://www.waveshare.com/esp32-s3-touch-amoled-1.43.htm?sku=30106) (see: [example build](/docs/builds/leaf-blaster.md))
    - [ZiNc Snowmote 2](/docs/builds/snowmote2.md) by ZiNc
    - [ZiNc Leaf Blaster remix case](https://www.printables.com/model/1265591) by ZiNc
    - [Leaf Blaster case](https://www.printables.com/model/1191785) by Markoblaster
- [WaveShare 1.28in LCD display](https://www.waveshare.com/esp32-s3-touch-lcd-1.28.htm)
    - [VX4 Case](https://www.printables.com/model/835158-pubmote) by ThePoro
    - Full development kit from [Avaspark](https://avaspark.com/products/pubmote-dev-kit) including a case, display, joystick, and other parts

- NO LONGER SUPPORTED: The ["Cowmote"](https://cowpowersystems.com/product/1) from ExcessRaccoon uses the [LilyGo T-Display S3 Amoled (1.43in)](https://lilygo.cc/products/t-display-s3-amoled-1-64?variant=44507650556085) (see: [example build](/docs/builds/snowmote.md))
    - [SnowMote case](https://www.printables.com/model/1143449) by ZiNc
    - [Finger Blaster case](https://www.printables.com/model/1159060) by Markoblaster

#### A Joystick. Options:
- [PS5 hall joystick](https://www.aliexpress.us/item/3256806823053436.html)
- [Nintendo Switch joystick](https://vi.aliexpress.com/item/1005006746686389.html)

#### VESC Express receiver. Options:
- [Building your own](https://forum.esk8.news/t/79789/17)
- [Trampa VESC Express Module](https://trampaboards.com/vesc-express--p-34857.html)
- [AvaSpark RGB Mini](https://avaspark.com/products/avaspark-rgb-mini)
- [CustomWheel VESC Express Module](https://customwheel.shop/accesories/vesc-express-module-wifi-bt)
- And many others...

## Setup and Usage

- Not sure where to get started with a Pubmote build? Check out an example build like the [Leaf Blaster](/docs/builds/leaf-blaster.md)!
- For instructions on first-time setup, pairing, and usage, see the [quick start guide](/docs/quick-start.md).

## Issues

[Create an issue](https://github.com/techfoundrynz/Pubmote/) on GitHub or post in the Pubmote channel within the PubWheel Discord server

## For Developers

- VS Code or other code editor
- PlatformIO
- SquareLine Studio

> [!TIP]
> If SL Studio repeatedly fails on startup because of a font error, try clearing the font bin files.
