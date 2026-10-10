# Board transport

Ownership: connectivity maintainers.

Public API: `remote/comms.h`; `remote/espnow.h` retains legacy API aliases. Owns
the BLE and ESP-NOW driver implementations, transport selection, framing, and
transport callbacks. Connection policy, persisted pairing data, UI and IP Wi-Fi
handoff orchestration belong to the application.

The settings adapter binds channel getter/setter callbacks once during boot,
before a driver or worker starts. The driver calls them synchronously during
ESP-NOW initialization to obtain and normalize the channel. NULL callbacks use
channel 1. Binding must not change while initialization is running; callbacks
must not block or re-enter the transport.

Runtime: receive and discovery callbacks run on SDK/driver tasks. Receive buffers
are borrowed for the callback duration; copy anything needed later. Send
completion can run on a driver task or synchronously on the sending task.
Serialize lifecycle changes through the application's radio-session worker and
stop board workers before switching to IP Wi-Fi. Deinitialization may block and
can fail; a failed teardown is not permission to start another driver.

Validation: BLE/ESP-NOW and radio-session host tests, firmware build, and repeated
BLE/ESP-NOW/IP Wi-Fi transitions on hardware.
