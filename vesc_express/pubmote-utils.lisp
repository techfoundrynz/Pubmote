@const-start

; Pubmote internal helpers.
; Depends on pubmote-consts.lisp and pubmote-vars.lisp.

; ---- Logging -------------------------------------------------------------
; One injected callback, no callback means silent.

(defun pubmote-log (level text) {
    (if (not-eq pubmote-log-cb nil) {
        (pubmote-log-cb level text)
    })
})

(defun pubmote-warn (text) {
    (pubmote-log PUBMOTE_LOG_WARN text)
})

(defun pubmote-info (text) {
    (pubmote-log PUBMOTE_LOG_INFO text)
})

; Gates debug only, checked before building a message so it costs no str-merge.
(defunret pubmote-log-on () {
    (if (eq pubmote-log-cb nil) (return nil))
    (if (eq pubmote-log-active-cb nil) (return t))
    (return (pubmote-log-active-cb))
})

; Rate limit: an unpaired remote nearby sends at input rate. The cell itself, so
; the timestamp goes back with setcdr rather than rebuilding the list.
(defunret pubmote-log-cell (key) {
    (loopforeach c pubmote-log-throttle
        (if (eq (car c) key) (return c)))
    (return nil)
})

(defunret pubmote-log-due (key secs) {
    (var cell (pubmote-log-cell key))
    (if (eq cell nil) (return t)) ; unknown key: never throttle
    (if (> (secs-since (cdr cell)) secs) {
        (setcdr cell (systime))
        (return t)
    })
    (return nil)
})

(defun pubmote-log-tick (key secs) {
    (and (pubmote-log-on) (pubmote-log-due key secs))
})

; ---- Host plumbing -------------------------------------------------------

(defunret pubmote-opt (opts key dflt) {
    (var v (assoc opts key))
    (if (eq v nil) (return dflt))
    (return v)
})

(defun pubmote-send-msg (text) {
    (if (not-eq pubmote-send-msg-cb nil) {
        (pubmote-send-msg-cb text)
    } {
        (print text)
    })
})

(defun pubmote-get-cfg (name) {
    (if (not-eq pubmote-get-config nil) {
        (pubmote-get-config name)
    })
})

(defun pubmote-set-cfg (name val) {
    (if (not-eq pubmote-set-config nil) {
        (pubmote-set-config name val)
    })
})

(defun pubmote-save-cfg () {
    (if (not-eq pubmote-save-config nil) {
        (pubmote-save-config)
    })
})

; ---- Internal helpers ----------------------------------------------------

(defunret pubmote-pack-u32 (byte-list) {
  (return (to-u32 (+ (shl (to-u32 (ix byte-list 0)) 24)
                     (shl (to-u32 (ix byte-list 1)) 16)
                     (shl (to-u32 (ix byte-list 2)) 8)
                     (to-u32 (ix byte-list 3)))))
})
(defunret pubmote-unpack-u32 (packed-value) {
  (return (list (to-byte (bitwise-and (shr packed-value 24) 0xFF))
                (to-byte (shr (bitwise-and packed-value 0xFF0000) 16))
                (to-byte (shr (bitwise-and packed-value 0xFF00) 8))
                (to-byte (bitwise-and packed-value 0xFF))))
})

(defun pubmote-serialize-telemetry (buf telemetry) {
    (bufset-u8 buf 5 (ix telemetry 0))       ; fault-code
    (bufset-i16 buf 6 (floor (* (ix telemetry 1) 10))) ; pitch-angle
    (bufset-i16 buf 8 (floor (* (ix telemetry 2) 10))) ; roll-angle
    (bufset-u8 buf 10 (ix telemetry 3))      ; state
    (bufset-u8 buf 11 (ix telemetry 4))      ; switch-state
    (bufset-i16 buf 12 (floor (* (ix telemetry 5) 10))) ; vin
    (bufset-i16 buf 14 (floor (ix telemetry 6)))     ; rpm
    (bufset-i16 buf 16 (floor (* (ix telemetry 7) 10))) ; speed
    (bufset-i16 buf 18 (floor (* (ix telemetry 8) 10))) ; tot-current
    (bufset-u8 buf 20 (floor (* (+ (abs (ix telemetry 9)) 0.5) 100))) ; duty-cycle-now
    (bufset-f32 buf 21 (ix telemetry 10) 'little-endian) ; distance-abs
    (bufset-u8 buf 25 (floor (* (ix telemetry 11) 2))) ; fet-temp-filtered
    (bufset-u8 buf 26 (floor (* (ix telemetry 12) 2))) ; motor-temp-filtered
    (bufset-u32 buf 27 (ix telemetry 13))    ; odometer
    (bufset-u8 buf 31 (floor (* (ix telemetry 14) 200))) ; battery-percent-remaining
})

; ---- WiFi channel pinning ------------------------------------------------
; ESP-NOW needs both ends on one channel, so station mode gets pinned while the
; link is live. That takes the station connection down, so a host that manages
; its own WiFi passes (wifi-channel-lock . off) and none of this runs.

(defun pubmote-lock-channel (reason) {
    (pubmote-info (str-merge "Channel switching disabled. Reason: " reason))
    (var ch (wifi-get-chan))
    (wifi-disconnect)
    (wifi-auto-reconnect nil)
    (wifi-set-chan ch)
    (setq pubmote-channel-locked ch)
    (pubmote-info (str-merge "Pinned to channel " (str-from-n ch)))
})

(defun pubmote-unlock-channel (reason) {
    (pubmote-info (str-merge "Channel switching enabled. Reason: " reason))
    (setq pubmote-channel-locked 0)
    (wifi-auto-reconnect true)
    (wifi-connect (conf-get 'wifi-sta-ssid) (conf-get 'wifi-sta-key))
})

(defun pubmote-is-station-mode () {
    (eq (conf-get 'wifi-mode) 1)
})

(defun pubmote-is-wifi-connected () {
    (eq (wifi-status) 'connected)
})

(defun pubmote-should-lock-channel () {
    ; Locking enabled by the host
    ; Channel is not locked
    ; Station mode
    ; Wifi is not connected
    (and pubmote-wifi-channel-lock (eq pubmote-channel-locked 0) (pubmote-is-station-mode) (not (pubmote-is-wifi-connected)))
})

(defun pubmote-should-unlock-channel (last-activity-time) {
    (if (and pubmote-wifi-channel-lock (> pubmote-channel-locked 0) (pubmote-is-station-mode) (> (secs-since last-activity-time) pubmote-channel-locked-timeout)) {
        (pubmote-unlock-channel (str-from-n pubmote-last-activity-time "Last activity time greater than set time"))
    })
})

; ---- Link state ----------------------------------------------------------

(defun pubmote-should-send-message () {
    (and
        (= pubmote-pairing-state PUBMOTE_PAIR_IDLE)
        (!= (pubmote-get-cfg 'pubmote-remote-mac-a) -1)
        (< (secs-since pubmote-last-activity-time) 1.0)
    )
})

; Valid destination for esp-now-send: 6 bytes and not the BLE placeholder MAC
(defun pubmote-is-valid-espnow-mac (mac) {
    (and (= (length mac) 6) (not-eq mac '(0 0 0 0 0 0)))
})

(defun pubmote-should-process-message (src data) {
    ; Length check must come before bufget-i32: an out-of-range bufget raises an eval error which would kill the event handler thread
    (and (= pubmote-pairing-state PUBMOTE_PAIR_IDLE) (>= (buflen data) 5) (eq pubmote-remote-mac src) (= (bufget-i32 data 1 'little-endian) (pubmote-get-cfg 'pubmote-secret-code)))
})

(defun pubmote-reset-last-activity-time () {
    (setq pubmote-last-activity-time (systime))
})

(defun pubmote-send-packet (dest-mac packet-buf is-ble) {
    (var send-buf (bufcreate (+ (buflen packet-buf) 1)))
    (bufset-u8 send-buf 0 PUBMOTE_MAGIC)
    (bufcpy send-buf 1 packet-buf 0 (buflen packet-buf))

    (if is-ble {
        (send-data send-buf 8)
    } {
        ; Never attempt an ESP-NOW send to the BLE placeholder (all-zeros) MAC
        (if (and pubmote-wifi-enabled (pubmote-is-valid-espnow-mac dest-mac)) {
            (mutex-lock pubmote-send-mutex)
            (var r (trap (esp-now-send dest-mac send-buf)))
            (mutex-unlock pubmote-send-mutex)
            ; esp-now-send fails when the peer is gone or the channel moved.
            ; The symptom is a remote that shows "connected" on the remote
            ; side but never receives telemetry.
            (if (and (eq (ix r 0) 'exit-error) (pubmote-log-tick 'rem-txerr 2.0))
                (pubmote-warn (str-merge "rem send " (to-str (ix r 1)))))
        })
    })
    (free send-buf)
})

@const-end
