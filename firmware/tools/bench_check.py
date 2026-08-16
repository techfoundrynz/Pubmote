"""
Guided on-air bench validation for the PubRemote comms pipeline.

Flash the remote (`pio run -t upload`), pair it with a board running
float_accessories, then run:

    python firmware/tools/bench_check.py COM4          # or your port
    python firmware/tools/bench_check.py COM4 --baud 115200

The script watches the serial log for "Connection state: X -> Y" transitions
(emitted by connection.c) and walks you through each scenario, timing the
recovery and comparing it against the bounds measured by comms_sim.py /
comms_sim_ble.py. Requires pyserial (`pip install pyserial`).

Scenarios:
  1. Boot connect            expect CONNECTED within 5s of reset
  2. Board power-cycle       expect RECONNECTING then CONNECTED; if the board
                             stays off >30s, DISCONNECTED then auto-reconnect
                             CONNECTED within ~15s of power-on
  3. Walk out of range/back  expect RECONNECTING while away, CONNECTED within
                             ~8s of return (ESP-NOW sweep) / ~4s (BLE)
  4. Board WiFi channel move expect CONNECTED within ~8s (ESP-NOW only)
  5. Pairing cancel          open Pair New, cancel; expect reconnection to the
                             previous board (CONNECTING then CONNECTED)
  6. Manual disconnect       menu Disconnect; expect NO reconnection for 60s
"""
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

STATE_RE = re.compile(r"Connection state: (\w+) -> (\w+)")

SCENARIOS = [
    ("Boot connect",
     "Reset the remote (or power it on) now.",
     "CONNECTED", 15.0, True),
    ("Board power-cycle (short)",
     "Power the board off, wait ~3 seconds, power it back on.",
     "CONNECTED", 20.0, True),
    ("Range recovery",
     "Take the remote out of range (or shield it) for ~10s, then bring it back.",
     "CONNECTED", 30.0, True),
    ("Channel move (ESP-NOW only - skip with 's' for BLE)",
     "Cause the board's WiFi to join an AP on a different channel (or toggle its WiFi).",
     "CONNECTED", 30.0, True),
    ("Pairing cancel restore",
     "On the remote: Menu > Boards > Pair New > (either transport) > Cancel.",
     "CONNECTED", 30.0, True),
    ("Manual disconnect stays disconnected",
     "On the remote: Menu > Disconnect. Then wait - there must be NO reconnection.",
     "CONNECTED", 60.0, False),  # expect_absent
]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    port = sys.argv[1]
    baud = 115200
    if "--baud" in sys.argv:
        baud = int(sys.argv[sys.argv.index("--baud") + 1])

    # Open without toggling DTR/RTS - those lines drive the ESP32 auto-reset
    # circuit and a default open would hard-reset the remote under test
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    print(f"Listening on {port} @ {baud}. Ctrl-C to abort.\n")
    results = []

    for name, instruction, target, window, expect_present in SCENARIOS:
        print(f"=== {name} ===")
        print(f"    {instruction}")
        ans = input("    Press Enter when ready (s to skip)... ").strip().lower()
        if ans == "s":
            results.append((name, None, "skipped"))
            continue
        ser.reset_input_buffer()
        t0 = time.monotonic()
        seen = None
        transitions = []
        while time.monotonic() - t0 < window:
            line = ser.readline().decode(errors="replace")
            m = STATE_RE.search(line)
            if m:
                dt = time.monotonic() - t0
                transitions.append(f"{m.group(1)}->{m.group(2)} @ {dt:.1f}s")
                if m.group(2) == target:
                    seen = dt
                    if expect_present:
                        break
        if expect_present:
            ok = seen is not None
            detail = f"{target} after {seen:.1f}s" if ok else f"no {target} within {window:.0f}s"
        else:
            ok = seen is None
            detail = ("stayed disconnected" if ok
                      else f"unexpected {target} after {seen:.1f}s")
        if transitions:
            detail += f"  [{', '.join(transitions)}]"
        results.append((name, ok, detail))
        print(f"    -> {'PASS' if ok else 'FAIL'}: {detail}\n")

    print("\n=== Bench summary ===")
    passed = sum(1 for _, ok, _ in results if ok)
    total = sum(1 for _, ok, _ in results if ok is not None)
    for name, ok, detail in results:
        tag = "SKIP" if ok is None else ("PASS" if ok else "FAIL")
        print(f"  [{tag}] {name} — {detail}")
    print(f"\n{passed}/{total} passed")


if __name__ == "__main__":
    main()
