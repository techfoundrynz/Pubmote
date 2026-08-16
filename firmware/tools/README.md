# Comms testing tools

Development tools for validating the connection/reconnection pipeline between
the remote (this firmware) and a board running the `float_accessories` VESC
Express package. Nothing here ships on the device.

Requirements: Python 3.x; `bench_check.py` additionally needs
`pip install pyserial`.

## comms_sim.py — ESP-NOW scenario simulation

Software-in-the-loop simulation of both endpoints' state machines, mirroring
the logic and timing constants of `connection.c` (state machine,
auto-reconnect + backoff), `transmitter.c` (send cadence: hunting vs settled
dedupe), `receiver.c` (channel-hop sweep, grace period, hop cursor) and the
board's `pubmote.lisp` loop (20Hz telemetry push, 1s activity window).

```
python firmware/tools/comms_sim.py
```

Runs 12 scenarios with asserted recovery bounds: boot connect (right and
wrong channel), sub-second RF gaps, short/long board outages, a mid-session
board channel move (WiFi joins an AP), sustained 30%/60% packet loss, manual
disconnect (must never auto-reconnect) and wake with a stale saved channel.
Scenarios suffixed `-OLD` are regression guards: they run the pre-fix
behavior and assert that it fails, proving the corresponding fix matters.

## comms_sim_ble.py — BLE scenario simulation

Same connection-layer model with a BLE link model instead of channels:
dial latency, service-discovery flow, zombie-link recovery, supervision
timeout, the reconnect-timer arming rules of `comms_ble.c`, and the BLE send
pacing of `transmitter.c`.

```
python firmware/tools/comms_sim_ble.py
```

13 scenarios: boot connect, range loss/recovery, discovery failures
(zombie-link recovery), long absence with auto-reconnect, repeated drops,
manual disconnect, host/controller reset, busy-stack retry failures, and
packet-loss flicker tests — including compatibility with boards running the
older (reply-only) package.

**Keep the constants in sync.** Both sims hand-copy timing constants from the
firmware sources (each constant is annotated with its origin). If you tune
`TX_RATE_MS`, any `connection.c`/`receiver.c` timing, or the transmitter
cadence rules, update the sims and re-run them — a green sim with stale
constants validates nothing.

## bench_check.py — guided on-air validation

Interactive checker for a real remote over USB serial. It watches the
firmware's `Connection state: X -> Y` log lines and walks you through six
on-air scenarios (boot connect, board power-cycle, range recovery, channel
move, pairing cancel/restore, manual disconnect), timing each recovery
against the bounds established by the sims.

```
pio run -e <your_env> -t upload
python firmware/tools/bench_check.py COM4        # your serial port
python firmware/tools/bench_check.py COM4 --baud 115200
```

Follow the prompts (`s` skips a scenario, e.g. the channel-move test when
paired over BLE). A pass/fail summary prints at the end.

## Serial-port gotcha: DTR/RTS auto-reset

Opening an ESP32 serial port with default settings asserts DTR/RTS, which
drives the auto-reset circuit and **hard-resets the remote** (repeatedly, if
a tool polls the port). `bench_check.py` already opens the port with both
lines deasserted; any other script or terminal you point at the remote must
do the same, e.g.:

```python
s = serial.Serial()          # do NOT pass the port to the constructor
s.port = "COM4"
s.baudrate = 115200
s.dtr = False                # set BEFORE open()
s.rts = False
s.open()
```
