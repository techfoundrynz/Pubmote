@const-start

; Pubmote: ESP-NOW / BLE tilt-remote link for the VESC Express.
;
; The library is host-agnostic: everything package-specific (where the
; config lives, what telemetry to send, what to do with control input,
; where log lines go) is injected through pubmote-setup. The host owns
; the event loop and routes packets in:
;
;   (pubmote-setup (list (cons 'on-control (fn (jsy jsx bt-c bt-z is-rev) ...))
;                        (cons 'get-telemetry (fn () ...))
;                        ...))
;   (spawn pubmote-loop)
;   ; from the host's event handler:
;   ;   event-esp-now-rx  -> (pubmote-rx src des data rssi)
;   ;   custom app data starting with PUBMOTE_MAGIC -> (pubmote-ble-rx data)
;
; Option keys, all optional:
;
;   vehicle-type      PUBMOTE_VEHICLE_* reported to the remote
;   on-control        input from the remote
;   get-telemetry     returns the 15-element telemetry list
;   send-msg          user-visible message; falls back to print
;   get/set/save-config   config access, see keys below
;   on-pairing-state  called with PUBMOTE_PAIR_* on every change
;   log               (level text), level is PUBMOTE_LOG_*; unset means silent
;   log-active        predicate; false skips building debug messages
;   wifi-channel-lock 'off to leave the station connection alone
;
; get/set/save-config persist these keys, whatever the storage is:
;   'pubmote-enabled 'pubmote-loop-delay 'pubmote-secret-code
;   'pubmote-remote-mac-a 'pubmote-remote-mac-b
;
; Pairing is driven with (pubmote-pair code) - code >= 0 starts pairing
; with that secret, -1 accepts, -2 rejects/aborts.

; Depends on pubmote-consts / -vars / -utils, loaded in that order.

(defun pubmote-setup (opts) {
    (setq pubmote-vehicle-type (pubmote-opt opts 'vehicle-type PUBMOTE_VEHICLE_UNSPECIFIED))
    (setq pubmote-on-control (pubmote-opt opts 'on-control nil))
    (setq pubmote-get-telemetry (pubmote-opt opts 'get-telemetry nil))
    (setq pubmote-send-msg-cb (pubmote-opt opts 'send-msg nil))
    (setq pubmote-get-config (pubmote-opt opts 'get-config nil))
    (setq pubmote-set-config (pubmote-opt opts 'set-config nil))
    (setq pubmote-save-config (pubmote-opt opts 'save-config nil))
    (setq pubmote-on-pairing-state (pubmote-opt opts 'on-pairing-state nil))
    (setq pubmote-log-cb (pubmote-opt opts 'log nil))
    (setq pubmote-log-active-cb (pubmote-opt opts 'log-active nil))
    ; Tests for an explicit 'off: assoc cannot tell absent from nil.
    (setq pubmote-wifi-channel-lock (not (eq (pubmote-opt opts 'wifi-channel-lock nil) 'off)))
})


(defun pubmote-set-pair-state (new-state) {
    ; Pairing is support-heavy and otherwise invisible - always reported.
    (if (!= new-state pubmote-pairing-state)
        (pubmote-info (str-merge "Pubmote pair state " (str-from-n new-state "%d"))))
    (setq pubmote-pairing-state new-state)
    (if (not-eq pubmote-on-pairing-state nil) {
        (pubmote-on-pairing-state new-state)
    })
})

; Connected / disconnected edge for the remote link.
(defun pubmote-log-transitions () {
    (var conn (pubmote-connected))
    (if (!= conn pubmote-prev-connected) {
        (setq pubmote-prev-connected conn)
        (pubmote-log PUBMOTE_LOG_DEBUG (if (= conn 1) "rem up" "rem down"))
    })
})

; ---- Connection state ----------------------------------------------------

; 1 while packets from the paired remote arrived within the last second
(defun pubmote-connected () {
    (if (< (secs-since pubmote-last-activity-time) 1) 1 0)
})

; ---- Lifecycle -----------------------------------------------------------

(defunret pubmote-init () {
    (if (eq pubmote-get-config nil) {
        (pubmote-send-msg "Pubmote: pubmote-setup must be called first")
        (return nil)
    })
    (setq pubmote-wifi-enabled (> (conf-get 'wifi-mode) 0))
    (setq pubmote-remote-mac (append (pubmote-unpack-u32 (pubmote-get-cfg 'pubmote-remote-mac-a)) (take (pubmote-unpack-u32 (pubmote-get-cfg 'pubmote-remote-mac-b)) 2)))
    ; A remote paired over BLE is stored with the all-zeros placeholder MAC
    (setq pubmote-ble-paired (= (pubmote-get-cfg 'pubmote-remote-mac-a) 0))
    (if pubmote-ble-paired {
        (pubmote-info "Pubmote BLE paired with remote")
    } {
        (pubmote-info (str-merge "Pubmote ESP-NOW paired with " (to-str pubmote-remote-mac)))
    })

    ; Read as bytes, convert to i so we can compare lists
    (loopfor i 0 (< i (length pubmote-remote-mac)) (+ i 1) {
        (setix pubmote-remote-mac i (to-i (ix pubmote-remote-mac i)))
    })

    (if (not pubmote-wifi-enabled) {
        (pubmote-send-msg "WiFi disabled. Pubmote running in BLE-only mode.")
    } {
        (esp-now-start)
        ; Skip the peer registration for BLE-paired remotes (placeholder MAC)
        (if (pubmote-is-valid-espnow-mac pubmote-remote-mac) {
            (esp-now-del-peer pubmote-remote-mac)
            (esp-now-add-peer pubmote-remote-mac)
        })
        (esp-now-del-peer pubmote-uni-mac)
        (esp-now-add-peer pubmote-uni-mac)
    })
    (return true)
})

(defunret pubmote-pair (pairing) {
    (cond
        ((>= pairing 0) {
            (pubmote-set-cfg 'pubmote-secret-code (to-i32 pairing))
            (setq pubmote-pairing-timer (systime))
            (pubmote-set-pair-state PUBMOTE_PAIR_INITIATED)
        })

        ; Pairing accepted
        ((= pairing -1) {
            (if (= (length pubmote-remote-mac) 6) {
                (pubmote-info (str-merge "Pubmote paired " (to-str pubmote-remote-mac)))
                (pubmote-set-cfg 'pubmote-remote-mac-a (pubmote-pack-u32 (take pubmote-remote-mac 4)))
                (pubmote-set-cfg 'pubmote-remote-mac-b (pubmote-pack-u32 (append (drop pubmote-remote-mac 4) '(0 0))))
            })
            (pubmote-save-cfg)
            (pubmote-init)
            (setq pubmote-pair-complete-status 1)
            (setq pubmote-send-pair-complete-retries 3)
            (pubmote-set-pair-state PUBMOTE_PAIR_IDLE)
        })

        ; Pairing rejected
        ((= pairing -2) {
            (pubmote-set-cfg 'pubmote-remote-mac-a -1)
            (pubmote-save-cfg)

            (setq pubmote-pair-complete-status 0)
            (setq pubmote-send-pair-complete-retries 3)

            (pubmote-set-pair-state PUBMOTE_PAIR_IDLE)

            ; Unlock wifi channel hopping
            (pubmote-should-unlock-channel pubmote-last-activity-time)
        })
    )

    (return true)
})

(defun pubmote-loop () {
    (if (pubmote-init) {
        (setq pubmote-loop-delay (pubmote-get-cfg 'pubmote-loop-delay))
        (if (< pubmote-loop-delay 1) {
            (pubmote-warn "rem bad rate, using 20Hz")
            (setq pubmote-loop-delay 20)
        })
        (var next-run-time (secs-since 0))
        (var loop-start-time 0)
        (var loop-end-time 0)
        (var pubmote-loop-delay-sec (/ 1.0 pubmote-loop-delay))
        (var data (bufcreate 33))

        (loopwhile t {
                (if (pubmote-get-cfg 'pubmote-enabled) {
                (setq pubmote-loop-ticks (+ pubmote-loop-ticks 1))
                ; Check last pubmote activity
                (pubmote-should-unlock-channel pubmote-last-activity-time)
                (pubmote-log-transitions)

                (setq loop-start-time  (secs-since 0))

                ; Escape as needed
                (if pubmote-exit-flag {
                    (break)
                })

                ; Send pair complete packets asynchronously without blocking QML
                (if (> pubmote-send-pair-complete-retries 0) {
                    (var tmpbuf (bufcreate 2))
                    (bufset-u8 tmpbuf 0 PUBMOTE_CMD_PAIR_COMPLETE)
                    (bufset-u8 tmpbuf 1 pubmote-pair-complete-status)
                    (pubmote-log PUBMOTE_LOG_DEBUG "rem pair complete tx")
                    (pubmote-send-packet pubmote-remote-mac tmpbuf nil)
                    (if (connected-ble) {
                        (pubmote-send-packet '() tmpbuf t)
                    })
                    (free tmpbuf)
                    (setq pubmote-send-pair-complete-retries (- pubmote-send-pair-complete-retries 1))

                    (if (= pubmote-send-pair-complete-retries 0) {
                        ; Only wipe the MAC if no new pairing handshake has
                        ; started in the meantime (the deferred wipe must not
                        ; clobber a fresh bond)
                        (if (and (= pubmote-pair-complete-status 0) (= pubmote-pairing-state PUBMOTE_PAIR_IDLE)) {
                            (setq pubmote-remote-mac '())
                        })
                    })
                })

                ; Timeout pairing process after set time has passed
                (if (and (> (secs-since pubmote-pairing-timer) pubmote-pairing-timer-timeout) (>= pubmote-pairing-state PUBMOTE_PAIR_INITIATED)) {
                    (pubmote-pair -2)
                })

                ; Pairing search
                (if (= pubmote-pairing-state PUBMOTE_PAIR_INITIATED) {
                    ; Update last activity time for pairing duration
                    (setq pubmote-last-activity-time (systime))

                    (if (> (- (systime) pubmote-last-pairing-broadcast) 50) {
                        (setq pubmote-last-pairing-broadcast (systime))
                        (if (pubmote-should-lock-channel) {
                            (pubmote-lock-channel "Begin pairing")
                        })

                        (var pairing-data (bufcreate 8))

                        (bufset-u8 pairing-data 0 PUBMOTE_CMD_PAIR_INIT)
                        (var local-mac (get-mac-addr))

                        (looprange i 0 6 {
                            (bufset-u8 pairing-data (+ i 1) (ix local-mac i))
                        })

                        ; Append our actual WiFi channel to prevent race condition with channel switching or channel bleed
                        ; send 0 there - BLE pairing ignores the byte
                        (bufset-u8 pairing-data 7 (if pubmote-wifi-enabled (wifi-get-chan) 0))

                        (pubmote-send-packet pubmote-uni-mac pairing-data nil)
                        (if (connected-ble) {
                            (pubmote-send-packet '() pairing-data t)
                        })
                        (free pairing-data)
                    })
                })

                ; Bond in progress
                (if (= pubmote-pairing-state PUBMOTE_PAIR_BONDING) {
                    ; Update last activity time for pairing duration
                    (setq pubmote-last-activity-time (systime))
                })

                ; Connected, send data
                (if (pubmote-should-send-message) {
                    (bufset-u8 data 0 PUBMOTE_CMD_SET_CORE_DATA)
                    (bufset-i32 data 1 (pubmote-get-cfg 'pubmote-secret-code))

                    (if (not-eq pubmote-get-telemetry nil) {
                        (pubmote-serialize-telemetry data (pubmote-get-telemetry))
                    })

                    (if (pubmote-log-tick 'rem-tx 2.0) {
                        (pubmote-log PUBMOTE_LOG_DEBUG (str-merge "rem tx, last rx "
                            (str-from-n (secs-since pubmote-last-activity-time) "%.2f")))
                    })
                    ; Push telemetry on the transport the remote paired with.
                    ; Pushing over BLE (rather than only replying to input
                    ; packets) keeps the remote's connection state solid even
                    ; when a single packet is lost.
                    (if pubmote-ble-paired {
                        (if (connected-ble) {
                            (pubmote-send-packet '() data t)
                        })
                    } {
                        (pubmote-send-packet pubmote-remote-mac data nil)
                    })
                })

                (setq loop-end-time (secs-since 0))
                (var actual-loop-time (- loop-end-time loop-start-time))
                (var time-to-wait (- next-run-time (secs-since 0)))

                (if (> time-to-wait 0) {
                    (yield (* time-to-wait 1000000))
                }{
                    (setq next-run-time (secs-since 0))
                })

                (setq next-run-time (+ next-run-time pubmote-loop-delay-sec))
            })
        })

        (free data)
        (setq pubmote-exit-flag nil)
    })
})

; ---- Packet handling -----------------------------------------------------

(defun pubmote-process-packet (data is-ble) {
    (var cmd (bufget-u8 data 0))

    (cond
        ((= cmd PUBMOTE_CMD_VERSION) {
            (if (= (buflen data) 8) {
                (pubmote-reset-last-activity-time)
                (setq pubmote-version (list (bufget-u8 data 5) (bufget-u8 data 6) (bufget-u8 data 7)))
                (pubmote-info (str-merge "Pubmote remote version " (to-str pubmote-version)))
            })
        })

        ((= cmd PUBMOTE_CMD_VERSION_REC) {
            (pubmote-reset-last-activity-time)
            (var tmpbuf (bufcreate 8))
            (bufset-u8 tmpbuf 0 PUBMOTE_CMD_VERSION_REC)
            (bufset-i32 tmpbuf 1 (pubmote-get-cfg 'pubmote-secret-code))
            (bufset-u16 tmpbuf 5 pubmote-api-version 'little-endian)
            (bufset-u8 tmpbuf 7 pubmote-vehicle-type)

            (pubmote-send-packet (if is-ble '() pubmote-remote-mac) tmpbuf is-ble)
            (free tmpbuf)
        })

        ((= cmd PUBMOTE_CMD_SET_INPUT_STATE) {
            (if (pubmote-log-tick 'rem-in 2.0) {
                (pubmote-log PUBMOTE_LOG_DEBUG "rem input rx")
            })
            (if (!= (buflen data) 17)
                (pubmote-log PUBMOTE_LOG_DEBUG (str-merge "rem bad input len " (str-from-n (buflen data) "%d"))))
            (if (= (buflen data) 17) {
                (pubmote-reset-last-activity-time)

                (var jsy (bufget-f32 data 5 'little-endian))
                (var jsx (bufget-f32 data 9 'little-endian))
                (var bt-c (bufget-u8 data 13))
                (var bt-z (bufget-u8 data 14))
                (var is-rev (bufget-u8 data 15))

                (if (not-eq pubmote-on-control nil) {
                    (pubmote-on-control jsy jsx bt-c bt-z is-rev)
                })
            })
        })

        (t {
            ; Gated: an unpaired remote nearby sends these at input rate and an ungated log floods the console.
            (if (and (not is-ble) (pubmote-log-on))
                (pubmote-log PUBMOTE_LOG_DEBUG (str-merge "rem cmd ? " (to-str cmd))))
        })
    )
})

(defun pubmote-rx (src des data rssi) {
    (if (and (pubmote-get-cfg 'pubmote-enabled) pubmote-wifi-enabled) {
        ; Verify and strip PUBMOTE_MAGIC
        (if (and (> (buflen data) 1) (= (bufget-u8 data 0) PUBMOTE_MAGIC)) {
            (bufcpy data 0 data 1 (-(buflen data) 1))
            (buf-resize data -1)

            (var cmd (bufget-u8 data 0))
            (if (pubmote-should-process-message src data) {
                ; Only lock the wifi channel for traffic from our paired
                ; remote (or during pairing below) - otherwise any device
                ; sending the magic byte could stall our wifi reconnection
                (if (pubmote-should-lock-channel) {
                    ; Update last activity time in case it does not establish a connection
                    (setq pubmote-last-activity-time (systime))

                    (pubmote-lock-channel "ESP-NOW packet received")
                })

                (pubmote-process-packet data nil)
            } {
                ; ESP-NOW specific pairing. Also re-respond while BONDING:
                ; the remote may have missed our first response (or retried
                ; pairing), and refusing to repeat it deadlocks the handshake
                ; until the pairing timeout expires.
                (if (= cmd PUBMOTE_CMD_PAIR_BOND) {
                    (if (or (= pubmote-pairing-state PUBMOTE_PAIR_INITIATED) (and (= pubmote-pairing-state PUBMOTE_PAIR_BONDING) (eq pubmote-remote-mac src))) {
                        (pubmote-info (str-merge "Pubmote bond req " (to-str src)))
                        (setq pubmote-remote-mac src)
                        (esp-now-add-peer pubmote-remote-mac)
                        (var tmpbuf (bufcreate 5))
                        (bufset-u8 tmpbuf 0 PUBMOTE_CMD_PAIR_BOND)
                        (bufset-i32 tmpbuf 1 (pubmote-get-cfg 'pubmote-secret-code))

                        (pubmote-send-packet pubmote-remote-mac tmpbuf nil)
                        (free tmpbuf)
                        (esp-now-del-peer pubmote-remote-mac)

                        (pubmote-set-pair-state PUBMOTE_PAIR_BONDING)
                    })
                } {
                    ; Not our remote, or our remote with the wrong secret
                    ; code. Distinguishing those two is exactly what people
                    ; need when a remote "connects to the wrong board".
                    (if (pubmote-log-tick 'rem-drop 2.0)
                        (pubmote-log PUBMOTE_LOG_DEBUG (str-merge "rem drop from " (to-str src)
                            (if (eq pubmote-remote-mac src) " bad code" " bad mac"))))
                })
            })
        })
    })
})

(defun pubmote-ble-rx (data) {
    (if (pubmote-get-cfg 'pubmote-enabled) {
        ; Verify and strip PUBMOTE_MAGIC
        (if (and (> (buflen data) 1) (= (bufget-u8 data 0) PUBMOTE_MAGIC)) {
            (var payload-len (- (buflen data) 1))
            (var payload (bufcreate payload-len))
            (bufcpy payload 0 data 1 payload-len)

            (var cmd (bufget-u8 payload 0))
            ; BLE doesn't check src/mac, but we verify pairing-state and secret code
            (if (and (= pubmote-pairing-state PUBMOTE_PAIR_IDLE) (>= payload-len 5) (= (bufget-i32 payload 1 'little-endian) (pubmote-get-cfg 'pubmote-secret-code))) {
                (pubmote-process-packet payload t)
            } {
                ; BLE pairing request
                (if (= cmd PUBMOTE_CMD_PAIR_BOND) {
                    ; Respond while INITIATED, or while BONDING if the bond in
                    ; progress is already a BLE one (all-zeros MAC): the remote
                    ; may have missed our first response, and refusing to
                    ; repeat it deadlocks the handshake until the pairing
                    ; timeout. The BONDING guard prevents a BLE request from
                    ; clobbering an in-progress ESP-NOW bond's MAC.
                    (if (or (= pubmote-pairing-state PUBMOTE_PAIR_INITIATED) (and (= pubmote-pairing-state PUBMOTE_PAIR_BONDING) (not (pubmote-is-valid-espnow-mac pubmote-remote-mac)))) {
                        (pubmote-info "Pubmote bond req (BLE)")
                        (setq pubmote-remote-mac '(0 0 0 0 0 0)) ; Initialize with dummy all-zeros MAC for BLE
                        (var tmpbuf (bufcreate 5))
                        (bufset-u8 tmpbuf 0 PUBMOTE_CMD_PAIR_BOND)
                        (bufset-i32 tmpbuf 1 (pubmote-get-cfg 'pubmote-secret-code))

                        (pubmote-send-packet '() tmpbuf t)
                        (free tmpbuf)

                        (pubmote-set-pair-state PUBMOTE_PAIR_BONDING)
                    } {
                        (pubmote-log PUBMOTE_LOG_DEBUG "rem bond req ignored, wrong state")
                    })
                })
                ; Throttled: a secret-code mismatch (remote paired to a
                ; different board) arrives at input rate, so this is gated
                ; and rate-limited rather than logged per packet.
                (if (and (!= cmd PUBMOTE_CMD_PAIR_BOND) (pubmote-log-tick 'rem-drop 2.0))
                    (pubmote-log PUBMOTE_LOG_DEBUG (str-merge "rem drop ble cmd " (to-str cmd))))
            })
            (free payload)
        })
    })
})

@const-end
