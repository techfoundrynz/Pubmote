"""
Software-in-the-loop simulation of the BLE side of the PubRemote comms
pipeline. Mirrors:
  - firmware/src/remote/comms_ble.c  (dial, discovery, zombie recovery,
                                      reconnect timer arming rules)
  - firmware/src/remote/connection.c (state machine, auto-reconnect)
  - firmware/src/remote/transmitter.c (hunting / stale-poke cadence)
  - float_accessories BLE path       (20Hz telemetry push when activity <1s,
                                      or legacy per-input response only)

Run: python firmware/tools/comms_sim_ble.py
"""
import random

# Remote connection layer (connection.c)
CONN_TASK_MS = 20
RECONNECTING_MS = 1000
TIMEOUT_MS = 30000
AUTO_RECONNECT_MS = 10000
TX_RATE_MS = 20
MAX_UPDATE_DELAY_MS = 500

# BLE driver (comms_ble.c) + physical timings
FAST_RETRY_MS = 500      # BLE_RECONNECT_FAST_DELAY_US
SLOW_RETRY_MS = 2000     # BLE_RECONNECT_SLOW_DELAY_US
DIAL_LATENCY_MS = 600    # connection establishment once peer advertises in range
DISCOVERY_MS = 500       # svc + chr + CCCD round trips
SUPERVISION_TIMEOUT_MS = 2000  # link drop detection after leaving range
DIAL_TIMEOUT_MS = 30000  # ble_gap_connect timeout

# Board (float_accessories)
BOARD_LOOP_MS = 50
BOARD_ACTIVITY_WINDOW_MS = 1000

DISCONNECTED, CONNECTING, CONNECTED, RECONNECTING = range(4)
LINK_IDLE, LINK_DIALING, LINK_DISCOVERY, LINK_UP, LINK_ZOMBIE = range(5)
STEP = 5


class BleSim:
    def __init__(self, rearm_fix=True, zombie_fix=True, board_push_fix=True,
                 remote_cadence_fix=True, discovery_fail_times=0, seed=1):
        self.rng = random.Random(seed)
        self.remote_cadence_fix = remote_cadence_fix
        self.busy_until = 0  # window where ble_gap_connect fails hard (EBUSY)
        # Remote connection layer
        self.state = DISCONNECTED
        self.last_state_change = 0
        self.last_updated = -10**9
        self.auto_reconnect = True
        self.last_send = -10**9
        # BLE driver
        self.link = LINK_IDLE
        self.link_since = 0
        self.has_target = False
        self.timer_at = None            # reconnect timer deadline (None = unarmed)
        self.synced = True
        self.discovery_fails_left = discovery_fail_times
        # Environment
        self.in_range = True
        self.left_range_at = None
        self.loss = 0.0
        # Board
        self.board_activity = -10**9
        # Fix flags
        self.rearm_fix = rearm_fix          # schedule_reconnect on all failure paths
        self.zombie_fix = zombie_fix        # terminate on discovery failure / zombie
        self.board_push_fix = board_push_fix  # board pushes 20Hz vs response-only

    # --- BLE driver ---
    def arm(self, t, delay):
        self.timer_at = t + delay  # schedule_reconnect: stop + start (overwrite)

    def old_arm(self, t, delay):
        # esp_timer_start_once fails silently if already armed (old behavior)
        if self.timer_at is None:
            self.timer_at = t + delay

    def rearm(self, t, delay):
        (self.arm if self.rearm_fix else self.old_arm)(t, delay)

    def connect_peer(self, t):
        # ble_driver_connect_peer
        self.has_target = True
        if self.link == LINK_UP:
            return
        if self.link == LINK_ZOMBIE:
            if self.zombie_fix:
                self.on_disconnect(t)  # terminate -> disconnect event
            return
        if self.link == LINK_IDLE and self.synced:
            self.link = LINK_DIALING
            self.link_since = t

    def on_disconnect(self, t):
        self.link = LINK_IDLE
        self.link_since = t
        if self.has_target:
            self.rearm(t, FAST_RETRY_MS if self.rearm_fix else SLOW_RETRY_MS)

    def ble_tick(self, t):
        # Reconnect timer
        if self.timer_at is not None and t >= self.timer_at:
            self.timer_at = None
            if self.has_target and self.link == LINK_IDLE:
                if not self.synced:
                    # OLD: silently skipped (stall). NEW: re-arm.
                    if self.rearm_fix:
                        self.rearm(t, SLOW_RETRY_MS)
                elif t < self.busy_until:
                    # ble_gap_connect hard failure (EBUSY/ENOMEM).
                    # OLD: logged, never retried. NEW: re-arm.
                    if self.rearm_fix:
                        self.rearm(t, SLOW_RETRY_MS)
                else:
                    self.link = LINK_DIALING
                    self.link_since = t
        # Dial progress
        if self.link == LINK_DIALING:
            if self.in_range and t - max(self.link_since,
                                         (self.back_in_range_at or 0)) >= DIAL_LATENCY_MS:
                self.link = LINK_DISCOVERY
                self.link_since = t
            elif t - self.link_since >= DIAL_TIMEOUT_MS:
                # connect event with failure status
                self.link = LINK_IDLE
                self.rearm(t, SLOW_RETRY_MS)
        # Discovery progress
        elif self.link == LINK_DISCOVERY:
            if t - self.link_since >= DISCOVERY_MS:
                if self.discovery_fails_left > 0:
                    self.discovery_fails_left -= 1
                    if self.zombie_fix:
                        self.on_disconnect(t)  # terminate -> clean retry
                    else:
                        self.link = LINK_ZOMBIE  # connected, NUS dead, no recovery
                        self.link_since = t
                else:
                    self.link = LINK_UP
                    self.link_since = t
        # Supervision timeout when out of range
        if self.link in (LINK_UP, LINK_ZOMBIE) and not self.in_range:
            if t - self.left_range_at >= SUPERVISION_TIMEOUT_MS:
                self.on_disconnect(t)

    # --- Connection layer (connection.c) ---
    def set_state(self, s, t):
        self.state = s
        self.last_state_change = t

    def connect_to_default(self, t):
        self.connect_peer(t)
        self.set_state(CONNECTING, t)

    def conn_task(self, t):
        if t % CONN_TASK_MS:
            return
        if self.state == DISCONNECTED:
            if self.auto_reconnect and t - self.last_state_change > AUTO_RECONNECT_MS:
                self.connect_to_default(t)
        elif self.state == CONNECTED:
            if t - self.last_updated > RECONNECTING_MS:
                self.set_state(RECONNECTING, t)
        elif self.state == CONNECTING:
            if t - self.last_state_change > TIMEOUT_MS:
                self.set_state(DISCONNECTED, t)
            elif self.last_updated > 0 and t - self.last_updated < RECONNECTING_MS:
                self.set_state(CONNECTED, t)
        elif self.state == RECONNECTING:
            if t - self.last_state_change > TIMEOUT_MS:
                self.set_state(DISCONNECTED, t)
            elif t - self.last_updated < RECONNECTING_MS:
                self.set_state(CONNECTED, t)

    # --- Transmitter (transmitter.c) ---
    def tx_task(self, t):
        if t % TX_RATE_MS:
            return False
        should = self.state in (CONNECTED, CONNECTING, RECONNECTING)
        if not should:
            self.last_send = t
            return False
        if self.remote_cadence_fix:
            # BLE flow control: unchanged data at 10Hz while hunting/stale,
            # 500ms keepalive when settled (transmitter.c BLE_POKE_INTERVAL_MS)
            stale = t - self.last_updated > MAX_UPDATE_DELAY_MS
            settled = self.state == CONNECTED and not stale
            min_interval = MAX_UPDATE_DELAY_MS if settled else 100
        else:
            min_interval = MAX_UPDATE_DELAY_MS  # old remote: 500ms keepalive always
        if t - self.last_send < min_interval:
            return False
        self.last_send = t
        return True

    # --- One simulation ---
    def run(self, duration_ms, events=()):
        self.back_in_range_at = 0
        self.connect_to_default(0)
        hist = []
        ev = sorted(events)
        ei = 0
        for t in range(0, duration_ms, STEP):
            while ei < len(ev) and ev[ei][0] <= t:
                ev[ei][1](self, t)
                ei += 1
            self.conn_task(t)
            sent = self.tx_task(t)
            delivered = (sent and self.link == LINK_UP and self.in_range
                         and self.rng.random() >= self.loss)
            if delivered:
                self.board_activity = t
                if not self.board_push_fix:
                    # Legacy: immediate telemetry response per input packet
                    if self.rng.random() >= self.loss and self.state != DISCONNECTED:
                        self.last_updated = t
            if self.board_push_fix and t % BOARD_LOOP_MS == 0:
                if (self.link == LINK_UP and self.in_range
                        and t - self.board_activity < BOARD_ACTIVITY_WINDOW_MS
                        and self.rng.random() >= self.loss
                        and self.state != DISCONNECTED):
                    self.last_updated = t
            self.ble_tick(t)
            hist.append((t, self.state, self.link))
        return hist


def leave_range(sim, t):
    sim.in_range = False
    sim.left_range_at = t

def enter_range(sim, t):
    sim.in_range = True
    sim.back_in_range_at = t


def first_state(hist, state, after=0):
    for t, s, _ in hist:
        if t >= after and s == state:
            return t
    return None


results = []
def check(name, cond, detail):
    results.append((name, cond, detail))

# B1: boot -> connected
h = BleSim().run(10000)
t = first_state(h, CONNECTED)
check("B1 BLE boot connects <=2.5s", t is not None and t <= 2500, f"connected at {t}ms")

# B2: out of range 5s -> recovers shortly after return
s = BleSim()
h = s.run(30000, events=[(5000, leave_range), (10000, enter_range)])
t = first_state(h, CONNECTED, after=10000)
check("B2 5s range loss recovers <=2.5s after return", t is not None and t - 10000 <= 2500,
      f"recovered {t-10000 if t else 'never'}ms after return")

# B3: discovery fails twice -> zombie-fix recovers
s = BleSim(discovery_fail_times=2)
h = s.run(30000)
t = first_state(h, CONNECTED)
check("B3 discovery double-failure recovers <=8s (zombie fix)", t is not None and t <= 8000,
      f"connected at {t}ms")

# B3-old: without zombie fix -> stuck forever
s = BleSim(discovery_fail_times=1, zombie_fix=False, rearm_fix=True)
h = s.run(120000)
t = first_state(h, CONNECTED)
check("B3-OLD zombie link never recovers (regression guard)", t is None, f"recovered at {t}")

# B4: long absence (40s) -> DISCONNECTED, then auto-reconnect after return
s = BleSim()
h = s.run(120000, events=[(5000, leave_range), (60000, enter_range)])
went_disc = first_state(h, DISCONNECTED, after=5000)
t = first_state(h, CONNECTED, after=60000)
check("B4 55s absence: recovers <=13s after return", went_disc is not None and t is not None
      and t - 60000 <= 13000, f"disconnected at {went_disc}, recovered {t-60000 if t else 'never'}ms after return")

# B5: flaky link - drop every 6s, back after 2s, five times -> always recovers
evs = []
for i in range(5):
    base = 5000 + i * 6000
    evs += [(base, leave_range), (base + 2000, enter_range)]
s = BleSim()
h = s.run(60000, events=evs)
last_recovery = first_state(h, CONNECTED, after=5000 + 4 * 6000 + 2000)
never_disc = first_state(h, DISCONNECTED, after=1000) is None
check("B5 five successive drops always recover, never DISCONNECTED",
      last_recovery is not None and never_disc, f"final recovery at {last_recovery}, never_disc={never_disc}")

# B6: manual disconnect -> BLE torn down, never re-dials
def manual_disconnect(sim, t):
    sim.auto_reconnect = False
    sim.set_state(DISCONNECTED, t)
    sim.has_target = False           # comms_disconnect_peer
    sim.timer_at = None
    if sim.link in (LINK_UP, LINK_ZOMBIE, LINK_DIALING, LINK_DISCOVERY):
        sim.link = LINK_IDLE
s = BleSim()
h = s.run(90000, events=[(5000, manual_disconnect)])
redial = any(l != LINK_IDLE for t, _, l in h if t > 5000)
check("B6 manual disconnect: link stays down for 85s", not redial, f"re-dialed={redial}")

# B7: host reset kills the link and sync; ble_on_sync's deferred connect
# recovers once the host comes back (both old and new code have this path)
def host_reset(sim, t):
    sim.synced = False
    sim.link = LINK_IDLE  # controller reset drops connections, no gap event
def host_sync(sim, t):
    sim.synced = True
    # ble_on_sync kicks a deferred connect if target set and link idle
    if sim.has_target and sim.link == LINK_IDLE:
        sim.link = LINK_DIALING
        sim.link_since = t
s = BleSim()
h = s.run(30000, events=[(5000, host_reset), (8000, host_sync)])
t = first_state(h, CONNECTED, after=8000)
check("B7 host reset recovers <=3s after sync returns", t is not None and t - 8000 <= 3000,
      f"recovered {t-8000 if t else 'never'}ms after sync")

# B7b: reconnect timer fires while the stack is busy (ble_gap_connect hard
# failure, e.g. EBUSY during a scan / transient ENOMEM).
# OLD: error logged, never re-armed -> permanent stall. NEW: re-arms, recovers.
def force_drop(sim, t):
    sim.on_disconnect(t)
def busy_5s(sim, t):
    sim.busy_until = t + 5000
s = BleSim()
h = s.run(60000, events=[(5000, busy_5s), (5001, force_drop)])
t = first_state(h, CONNECTED, after=10000)
check("B7b busy-stack retry failure recovers <=8s after busy clears (re-arm fix)",
      t is not None and t - 10000 <= 8000, f"recovered {t-10000 if t else 'never'}ms after busy cleared")

s = BleSim(rearm_fix=False)
h = s.run(120000, events=[(5000, busy_5s), (5001, force_drop)])
# Note: with auto-reconnect at the connection layer, even the old driver-stall
# is eventually rescued (connect_peer re-dials). Assert the DRIVER stalled by
# checking recovery only happens after the connection layer's 30s+10s cycle,
# i.e. much later than the busy window clearing.
t = first_state(h, CONNECTED, after=10000)
check("B7b-OLD driver stalls; only the new auto-reconnect layer rescues it late (regression guard)",
      t is None or t > 40000, f"recovered at {t}")

# B8: 20% loss - board 20Hz push: no flicker
s = BleSim(seed=42)
s.loss = 0.20
h = s.run(60000)
flicker = first_state(h, RECONNECTING, after=3000)
check("B8 20% loss with board push: no RECONNECTING flicker in 60s", flicker is None,
      f"flickered at {flicker}")

# B9: NEW remote + OLD (response-only) board -> stale poke compensates, no flicker.
# Proves compatibility with boards that haven't updated the package yet.
s = BleSim(board_push_fix=False, seed=42)
s.loss = 0.20
h = s.run(60000)
flicker = first_state(h, RECONNECTING, after=3000)
check("B9 new remote + old board at 20% loss: stale poke prevents flicker", flicker is None,
      f"flickered at {flicker}")

# B9-OLD: old remote + old board -> keepalive-only exchange flickers under loss
s = BleSim(board_push_fix=False, remote_cadence_fix=False, seed=42)
s.loss = 0.20
h = s.run(60000)
flicker = first_state(h, RECONNECTING, after=3000)
check("B9-OLD old remote + old board flickers under 20% loss (regression guard)",
      flicker is not None, f"flickered at {flicker}")

print(f"{'PASS' if all(c for _, c, _ in results) else 'FAIL'}: {sum(1 for _, c, _ in results if c)}/{len(results)}")
for name, cond, detail in results:
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} — {detail}")
