# Pubmote Library Usage Instructions

The Pubmote receiver library is designed to be imported and used as a standalone library by other VESC Lisp packages. It supports remotes connected over ESP-NOW or BLE.

For a complete real-world integration, see the `float_accessories` VESC package (`float_accessories.lisp`, `lib/utils.lisp` event handler, `lib/commands.lisp` data-rx dispatch, and `ui.qml` pairing UI).

The library is split across four files, which must be loaded in this order (later files depend on definitions from earlier ones):

| File | Contents |
|------|----------|
| `pubmote-consts.lisp` | Protocol command IDs, pairing states, vehicle types, `PUBMOTE_MAGIC` |
| `pubmote-vars.lisp` | Runtime state variables and callback slots |
| `pubmote-utils.lisp` | Helpers: config access, logging, telemetry serialization, wifi channel locking, packet send |
| `pubmote.lisp` | Public API: `pubmote-setup`, `pubmote-loop`, `pubmote-pair`, RX handlers |

Every name the library defines is prefixed `pubmote-` or `PUBMOTE_`, since LispBM
has one global namespace to share with the host.

## Integration Steps

1. **Import and load all four library files** in your host script, in order:
   ```lisp
   (import "lib/pubmote-consts.lisp" 'pubmote-consts)
   (import "lib/pubmote-vars.lisp" 'pubmote-vars)
   (import "lib/pubmote-utils.lisp" 'pubmote-utils)
   (import "lib/pubmote.lisp" 'pubmote)

   (read-eval-program pubmote-consts)
   (read-eval-program pubmote-vars)
   (read-eval-program pubmote-utils)
   (read-eval-program pubmote)
   ```

2. **Initialize callbacks and options** using `pubmote-setup`. Options are an
   assoc list; every key is optional:
   ```lisp
   (pubmote-setup (list
       (cons 'vehicle-type     PUBMOTE_VEHICLE_ONEWHEEL)
       (cons 'on-control       (fn (jsy jsx bt-c bt-z is-rev) { ... }))
       (cons 'get-telemetry    (fn () { ... }))
       (cons 'send-msg         (fn (text) { ... }))
       (cons 'get-config       (fn (name) { ... }))
       (cons 'set-config       (fn (name val) { ... }))
       (cons 'save-config      (fn () { ... }))
       (cons 'on-pairing-state (fn (state) { ... }))
       (cons 'log              (fn (level text) { ... }))
       (cons 'log-active       (fn () ... ))
       (cons 'wifi-channel-lock 'off)))
   ```

   | Key | Purpose |
   |-----|---------|
   | `vehicle-type` | `PUBMOTE_VEHICLE_*` reported to the remote (default unspecified) |
   | `on-control` | Input from the remote |
   | `get-telemetry` | Returns the 15-element telemetry list |
   | `send-msg` | User-visible message; falls back to `print` when unset |
   | `get-config` / `set-config` / `save-config` | Config access, see keys below |
   | `on-pairing-state` | Called with `PUBMOTE_PAIR_*` on every change |
   | `log` | `(level text)`, level is `PUBMOTE_LOG_*`; unset means silent |
   | `log-active` | Predicate; see Logging below |
   | `wifi-channel-lock` | `'off` to leave the station connection alone entirely |

3. **Register the receive handlers.** The library defines `pubmote-rx` (ESP-NOW) and `pubmote-ble-rx` (BLE / custom app data), but the host script owns event registration:
   ```lisp
   (defun event-handler ()
       (loopwhile t
           (recv
               ((event-esp-now-rx (? src) (? des) (? data) (? rssi)) (pubmote-rx src des data rssi))
               ((event-data-rx . (? data)) (pubmote-ble-rx data))
               (_ nil)
   )))

   (event-register-handler (spawn event-handler))
   (event-enable 'event-esp-now-rx)
   (event-enable 'event-data-rx)
   ```
   Notes:
   - If your package also receives its own QML/app commands over `event-data-rx`, dispatch on the first byte: Pubmote packets always start with `PUBMOTE_MAGIC` (169). See `command-rx` in float_accessories' `lib/commands.lisp`.
   - Consider wrapping each handler call in `trap` and monitoring the event thread for restarts, so a malformed packet can never kill event handling (see `dispatch-trapped` / `spawn-event-handler-with-restart` in float_accessories).

4. **Spawn the background loop** thread:
   ```lisp
   (setq pubmote-context-id (spawn pubmote-loop))
   ```

## Required Config Keys

The `get-config` / `set-config` / `save-config` callbacks must back the following keys (persisted by the host package, e.g. in eeprom):

- `pubmote-enabled` — master enable; when false the loop idles and RX packets are ignored (default 0)
- `pubmote-remote-mac-a` — first 4 bytes of the paired remote's MAC, packed as i32 (default `-1` = unpaired, `0` = BLE-paired placeholder)
- `pubmote-remote-mac-b` — last 2 bytes of the paired MAC, packed as i32 (default `-1`)
- `pubmote-secret-code` — i32 shared secret established during pairing; validated on every packet (default `-1`)
- `pubmote-loop-delay` — loop rate in Hz (default 20; values below 1 fall back to 20)

`save-config` only needs to persist the pairing-related keys (`pubmote-remote-mac-a`, `pubmote-remote-mac-b`, `pubmote-secret-code`) — those are the only values the library changes.

## Pairing

Drive pairing from your app/QML layer via `pubmote-pair` (e.g. float_accessories' `ui.qml` sends `(pubmote-pair N)` as eval'd code):

- `(pubmote-pair code)` with `code >= 0` — start pairing, using `code` as the new secret code. The library broadcasts pairing packets over ESP-NOW and responds to BLE pairing requests.
- `(pubmote-pair -1)` — accept: persists the remote's MAC and secret code, then re-initializes.
- `(pubmote-pair -2)` — reject/cancel: clears the stored MAC.

Pairing times out automatically after 60 seconds. Every state change is passed to the `on-pairing-state` callback as `PUBMOTE_PAIR_IDLE` (0), `PUBMOTE_PAIR_INITIATED` (1) or `PUBMOTE_PAIR_BONDING` (2) — what the host does with it is its own business. float_accessories forwards it to the connected app via `send-data` as `"pairing-status N"`.

A remote paired over BLE is stored with the all-zeros placeholder MAC (`pubmote-remote-mac-a` = 0); telemetry is then pushed over the BLE connection instead of ESP-NOW. If WiFi is disabled on the Express (`wifi-mode` 0), the library runs in BLE-only mode and skips all ESP-NOW setup.

## Telemetry Format

`get-telemetry` must return a 15-element list, in order:

```
fault-code, pitch-angle, roll-angle, state, switch-state, input-voltage,
rpm, speed, total-current, duty-cycle, distance-abs, fet-temp, motor-temp,
odometer, battery-percent (0.0 - 1.0)
```

## Supported Vehicle Types

Vehicle types are defined as constants in `pubmote-consts.lisp`; pass one as the `vehicle-type` option to `pubmote-setup`:
- `PUBMOTE_VEHICLE_UNSPECIFIED` (0)
- `PUBMOTE_VEHICLE_ONEWHEEL` (1)
- `PUBMOTE_VEHICLE_ESKATE` (2)
- `PUBMOTE_VEHICLE_SCOOTER` (3)
- `PUBMOTE_VEHICLE_EUC` (4)

## Logging

Everything goes through the `log` callback, called as `(level text)`. No callback
means silent.

| Level | Meaning |
|-------|---------|
| `PUBMOTE_LOG_ERROR` (0) | Reserved; the library does not currently emit it |
| `PUBMOTE_LOG_WARN` (1) | Failure the user needs to see |
| `PUBMOTE_LOG_INFO` (2) | Pairing and link events; expected to always reach the console |
| `PUBMOTE_LOG_DEBUG` (3) | Verbose diagnostics, for the host to gate |

Lines that can arrive at remote-input rate are rate-limited to one per two
seconds internally. `log-active` is an optional predicate gating debug only,
checked before the message is built so it costs no `str-merge`. Routing into a
category logger:

```lisp
(cons 'log        (fn (level text) {
    (cond ((= level PUBMOTE_LOG_ERROR) (dbg-err text))
          ((= level PUBMOTE_LOG_WARN) (dbg-warn text))
          ((= level PUBMOTE_LOG_INFO) (print text))
          (t (dbg DBG-REM text)))
}))
(cons 'log-active (fn () (dbg-active DBG-REM)))
```

## WiFi Channel Locking

When the Express is in station mode but not connected to WiFi, incoming remote traffic locks the current WiFi channel (disables auto-reconnect) so ESP-NOW stays on a stable channel. The lock is released automatically after 10 seconds of remote inactivity, allowing normal WiFi reconnection to resume. No host integration is required, but be aware WiFi reconnection is deferred while a remote is active.

The library reconnects with `(conf-get 'wifi-sta-ssid)` / `(conf-get 'wifi-sta-key)`.
A host that manages its own WiFi passes `(cons 'wifi-channel-lock 'off)` to disable
all of it, at the cost of ESP-NOW dropping out when the station hops channels.
