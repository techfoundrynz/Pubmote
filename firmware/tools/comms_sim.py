"""
Software-in-the-loop simulation of the PubRemote <-> float_accessories comms
pipeline. Faithfully mirrors the state machines and timing constants of:
  - firmware/src/remote/connection.c   (connection_task, auto-reconnect)
  - firmware/src/remote/transmitter.c  (tx cadence, hunting vs connected dedupe)
  - firmware/src/remote/receiver.c     (channel hop sweep, grace, hop cursor)
  - float_accessories lib/pubmote.lisp (20Hz loop, 1s activity window)

Time base: 5ms steps (gcd of all task periods).
"""
import random

# --- Constants mirrored from source ---
TX_RATE_MS = 20                  # platformio.ini
MAX_UPDATE_DELAY_MS = 500        # transmitter.c
CONN_TASK_MS = 20                # connection.c CONNECTION_TIMER_DELAY_MS
RECONNECTING_MS = 1000           # connection.c RECONNECTING_DURATION_MS
TIMEOUT_MS = 30000               # connection.c TIMEOUT_DURATION_MS
AUTO_RECONNECT_MS = 10000        # connection.c AUTO_RECONNECT_INTERVAL_MS
RX_TASK_MS = 5                   # receiver.c RECEIVER_TASK_DELAY_MS
HOP_INTERVAL_MS = 200            # receiver.c CHANNEL_HOP_INTERVAL_MS
HOP_GRACE_MS = 3000              # receiver.c RECONNECT_HOP_GRACE_MS
NUM_CHANNELS = 14
BOARD_LOOP_MS = 50               # pubmote-loop-delay 20Hz
BOARD_ACTIVITY_WINDOW_MS = 1000  # should-send-message < 1.0s

DISCONNECTED, CONNECTING, CONNECTED, RECONNECTING = range(4)
STATE_NAMES = ["DISCONNECTED", "CONNECTING", "CONNECTED", "RECONNECTING"]

STEP = 5


class Remote:
    def __init__(self, saved_channel, hunting_tx_fix=True, reconnect_hop_fix=True,
                 auto_reconnect_fix=True, connect_sets_channel_fix=True):
        self.state = DISCONNECTED
        self.last_state_change = 0
        self.last_updated = -10**9   # remoteStats.lastUpdated (never)
        self.saved_channel = saved_channel  # pairing_settings.channel
        self.radio_channel = saved_channel
        self.hop_accum = 0
        self.hop_cursor = 0
        self.last_send = -10**9
        self.auto_reconnect = True
        # Feature flags to contrast old vs fixed behavior
        self.hunting_tx_fix = hunting_tx_fix
        self.reconnect_hop_fix = reconnect_hop_fix
        self.auto_reconnect_fix = auto_reconnect_fix
        self.connect_sets_channel_fix = connect_sets_channel_fix

    def set_state(self, s, t):
        self.state = s
        self.last_state_change = t

    def connect_to_default(self, t):
        # connection_connect_to_peer
        if self.connect_sets_channel_fix:
            self.radio_channel = self.saved_channel
        self.set_state(CONNECTING, t)

    def conn_task(self, t):
        if t % CONN_TASK_MS:
            return
        if self.state == DISCONNECTED:
            if (self.auto_reconnect_fix and self.auto_reconnect and
                    t - self.last_state_change > AUTO_RECONNECT_MS):
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

    def receiver_task(self, t, received_this_step):
        if t % RX_TASK_MS:
            return
        if received_this_step:
            self.hop_accum = 0
            return
        is_connecting = self.state == CONNECTING
        stale = (self.reconnect_hop_fix and self.state == RECONNECTING and
                 t - self.last_updated > HOP_GRACE_MS)
        if is_connecting or stale:
            if self.hop_accum > HOP_INTERVAL_MS:
                if self.hop_cursor == 0:
                    self.hop_cursor = self.saved_channel
                self.hop_cursor = (self.hop_cursor % NUM_CHANNELS) + 1
                # change_channel: commit on success (all channels valid here)
                self.radio_channel = self.hop_cursor
                self.saved_channel = self.hop_cursor
                self.hop_accum = 0
            else:
                self.hop_accum += RX_TASK_MS
        else:
            self.hop_accum = 0
            self.hop_cursor = 0

    def transmitter_task(self, t):
        """Returns True if an input-state packet is transmitted this tick."""
        if t % TX_RATE_MS:
            return False
        should_transmit = self.state in (CONNECTED, CONNECTING, RECONNECTING)
        if not should_transmit:
            self.last_send = t  # reset last send time (transmitter.c else branch)
            return False
        # Dedupe: idle joystick -> unchanged data
        if self.hunting_tx_fix:
            telemetry_stale = t - self.last_updated > MAX_UPDATE_DELAY_MS
            dedupe = self.state == CONNECTED and not telemetry_stale
        else:
            dedupe = True  # old behavior: dedupe in every state
        if dedupe and t - self.last_send < MAX_UPDATE_DELAY_MS:
            return False
        self.last_send = t
        return True


class Board:
    def __init__(self, channel):
        self.on = True
        self.channel = channel
        self.last_activity = -10**9

    def hears(self, remote, loss, rng):
        return (self.on and remote.radio_channel == self.channel
                and rng.random() >= loss)

    def board_loop_sends(self, t):
        # pubmote-loop: telemetry push when activity < 1s
        if t % BOARD_LOOP_MS:
            return False
        return self.on and (t - self.last_activity) < BOARD_ACTIVITY_WINDOW_MS


def run(scenario, duration_ms, saved_channel=3, board_channel=3, loss=0.0,
        seed=1, events=(), **remote_flags):
    """events: list of (t_ms, fn(remote, board)) applied at time t."""
    rng = random.Random(seed)
    r = Remote(saved_channel, **remote_flags)
    b = Board(board_channel)
    r.connect_to_default(0)  # connection_init: paired -> connect
    history = []
    ev = sorted(events)
    ei = 0
    for t in range(0, duration_ms, STEP):
        while ei < len(ev) and ev[ei][0] <= t:
            ev[ei][1](r, b)
            ei += 1
        r.conn_task(t)
        sent = r.transmitter_task(t)
        if sent and b.hears(r, loss, rng):
            b.last_activity = t
        received = False
        if b.board_loop_sends(t):
            # Delivery requires channel match; remote processes telemetry only
            # in CONNECTED/CONNECTING/RECONNECTING (process_board_data gate)
            if (r.radio_channel == b.channel and rng.random() >= loss
                    and r.state != DISCONNECTED):
                r.last_updated = t
                received = True
        r.receiver_task(t, received)
        history.append((t, r.state))
    return r, b, history


def first_time_in(history, state, after=0):
    for t, s in history:
        if t >= after and s == state:
            return t
    return None


def states_between(history, t0, t1):
    return set(s for t, s in history if t0 <= t <= t1)


results = []

def check(name, cond, detail):
    results.append((name, cond, detail))


# S1: boot, board on same channel -> connect fast
r, b, h = run("s1", 10000)
t = first_time_in(h, CONNECTED)
check("S1 boot same-channel connects <1.5s", t is not None and t <= 1500, f"connected at {t}ms")

# S2: boot, board on a different channel -> one-sweep connect
worst = 0
for ch in range(1, 15):
    r, b, h = run("s2", 20000, saved_channel=3, board_channel=ch)
    t = first_time_in(h, CONNECTED)
    assert t is not None, f"S2 never connected for ch {ch}"
    worst = max(worst, t)
check("S2 boot wrong-channel connects <=4s (worst over all 14)", worst <= 4000, f"worst {worst}ms")

# S2-old: same with OLD hunting keepalive (dedupe while connecting)
worst_old = 0
for ch in range(1, 15):
    r, b, h = run("s2o", 40000, saved_channel=3, board_channel=ch, hunting_tx_fix=False)
    t = first_time_in(h, CONNECTED)
    worst_old = max(worst_old, t if t is not None else 40000)
check("S2-OLD hunting keepalive is slower (regression guard)", worst_old > worst,
      f"old worst {worst_old}ms vs fixed {worst}ms")

# S3: brief 800ms board outage -> no visible state change
def board_off(rm, bd): bd.on = False
def board_on(rm, bd): bd.on = True
r, b, h = run("s3", 20000, events=[(5000, board_off), (5800, board_on)])
mid = states_between(h, 5000, 8000)
check("S3 800ms dropout invisible (never leaves CONNECTED)", mid == {CONNECTED}, f"states seen {sorted(STATE_NAMES[s] for s in mid)}")

# S4: 2s outage -> RECONNECTING then recovery within 1.5s of return
r, b, h = run("s4", 30000, events=[(5000, board_off), (7000, board_on)])
t = first_time_in(h, CONNECTED, after=7000)
check("S4 2s outage recovers <=1.5s after board returns", t is not None and t - 7000 <= 1500, f"recovered {t-7000 if t else 'never'}ms after return")

# S5: board changes channel while connected (WiFi joined AP)
def move_channel(rm, bd): bd.channel = 11
r, b, h = run("s5", 40000, events=[(5000, move_channel)])
drop = first_time_in(h, RECONNECTING, after=5001)
t = first_time_in(h, CONNECTED, after=drop) if drop else None
went_disc = first_time_in(h, DISCONNECTED, after=5001)
check("S5 channel move recovers <=8s, never DISCONNECTED",
      drop is not None and t is not None and t - 5000 <= 8000 and went_disc is None,
      f"dropped at {drop}, recovered {t-5000 if t else 'never'}ms after move")

# S5-old: without reconnect-hop -> stuck: never returns to CONNECTED after drop
r, b, h = run("s5o", 60000, events=[(5000, move_channel)], reconnect_hop_fix=False, auto_reconnect_fix=False)
drop = first_time_in(h, RECONNECTING, after=5001)
t = first_time_in(h, CONNECTED, after=drop) if drop else None
check("S5-OLD (no reconnect hop/auto) never recovers (regression guard)", drop is not None and t is None,
      f"old dropped at {drop}, recovered at {t}")

# S6: board off for 60s -> auto-reconnect brings it back
r, b, h = run("s6", 120000, events=[(5000, board_off), (65000, board_on)])
t = first_time_in(h, CONNECTED, after=65000)
check("S6 60s absence: auto-reconnect recovers <=15s after return", t is not None and t - 65000 <= 15000, f"recovered {t-65000 if t else 'never'}ms after return")

# S7: sustained 30% packet loss -> stays connected for 60s
r, b, h = run("s7", 60000, loss=0.30, seed=42)
bad = first_time_in(h, DISCONNECTED, after=3000)
check("S7 30% loss never drops to DISCONNECTED over 60s", bad is None, f"disconnected at {bad}")

# S7b: 60% loss stress -- must still not reach DISCONNECTED (RECONNECTING ok)
r, b, h = run("s7b", 60000, loss=0.60, seed=7)
bad = first_time_in(h, DISCONNECTED, after=3000)
check("S7b 60% loss never drops to DISCONNECTED over 60s", bad is None, f"disconnected at {bad}")

# S8: manual disconnect -> no reconnection ever
def manual_disconnect(rm, bd):
    rm.auto_reconnect = False
    rm.set_state(DISCONNECTED, 5000)
r, b, h = run("s8", 90000, events=[(5000, manual_disconnect)])
t = first_time_in(h, CONNECTING, after=5001)
check("S8 manual disconnect never auto-reconnects (85s observed)", t is None, f"reconnected at {t}")

# S9: remote sleeps/wakes (fresh boot object) with stale saved channel after
# board moved while remote was asleep
r, b, h = run("s9", 20000, saved_channel=4, board_channel=9)
t = first_time_in(h, CONNECTED)
check("S9 wake with stale channel connects <=4s", t is not None and t <= 4000, f"connected at {t}ms")

print(f"{'PASS' if all(c for _, c, _ in results) else 'FAIL'}: {sum(1 for _, c, _ in results if c)}/{len(results)}")
for name, cond, detail in results:
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} — {detail}")
