; Serialises esp-now-send across contexts - see pubmote-send-packet.
; Declared above @const-start deliberately: a mutex is a cons cell that
; mutex-lock/unlock mutate in place, so it must not be allocated in the
; constant heap.
(def pubmote-send-mutex (mutex-create))

; Same reason - setassoc mutates in place, and only updates existing keys.
(def pubmote-log-throttle (list (cons 'rem-txerr 0)
                                (cons 'rem-tx 0)
                                (cons 'rem-in 0)
                                (cons 'rem-drop 0)))

@const-start

; Pubmote runtime state and host callback slots.
; Depends on pubmote-consts.lisp.

; State
(def pubmote-loop-delay)  ; Loop rate in Hz, read from the config
(def pubmote-pairing-state PUBMOTE_PAIR_IDLE)
(def pubmote-exit-flag nil)
(def pubmote-send-pair-complete-retries 0)
(def pubmote-pair-complete-status 0)
(def pubmote-last-pairing-broadcast 0)
(def pubmote-last-activity-time 0)
(def pubmote-wifi-enabled nil)
(def pubmote-remote-mac '())
(def pubmote-ble-paired nil)
(def pubmote-pairing-timer 31)
(def pubmote-pairing-timer-timeout 60) ; How many seconds to wait before aborting pairing
(def pubmote-uni-mac '(255 255 255 255 255 255)) ; Universal mac (all devices)
(def pubmote-channel-locked 0)
(def pubmote-channel-locked-timeout 10) ; How many seconds of no activity to wait before unlocking locked wifi channel
(def pubmote-version '(0 0 0))
(def pubmote-api-version 1)
(def pubmote-vehicle-type PUBMOTE_VEHICLE_UNSPECIFIED)
(def pubmote-prev-connected -1)
(def pubmote-loop-ticks 0) ; Loop iterations, for a host that wants a rate

; Options, set by pubmote-setup
(def pubmote-wifi-channel-lock t)

; Host callbacks, injected by pubmote-setup
(def pubmote-on-control nil)
(def pubmote-get-telemetry nil)
(def pubmote-send-msg-cb nil)
(def pubmote-get-config nil)
(def pubmote-set-config nil)
(def pubmote-save-config nil)
(def pubmote-on-pairing-state nil)
(def pubmote-log-cb nil)
(def pubmote-log-active-cb nil)

@const-end
