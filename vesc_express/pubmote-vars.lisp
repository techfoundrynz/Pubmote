;@const-symbol-strings

@const-start

(def pubmote-loop-delay)  ; Loop delay in microseconds (100ms)
(def pairing-state PAIR_STATE_IDLE)
(def pubmote-exit-flag nil)
(def pubmote-send-pair-complete-retries 0)
(def pubmote-pair-complete-status 0)
(def pubmote-last-pairing-broadcast 0)
(def pubmote-last-activity-time (systime))
(def wifi-enabled-on-boot nil)
(def pubmote-remote-mac '())
(def pubmote-ble-paired nil)
(def pubmote-pairing-timer 31)
(def pubmote-pairing-timer-timeout 60) ; How many seconds to wait before aborting pairing (increased to 60s)
(def uni-mac '(255 255 255 255 255 255)) ; Universal mac (all devices)
(def channel-locked 0)
(def channel-locked-timeout 10) ; How many seconds of no activity to wait before unlocking locked wifi channel
(def pubmote-version '(0 0 0))
(def pubmote-api-version 1)
(def pubmote-vehicle-type VEHICLE_TYPE_UNSPECIFIED)

(def pubmote-on-control nil)
(def pubmote-get-telemetry nil)
(def pubmote-send-msg-cb nil)
(def pubmote-get-config nil)
(def pubmote-set-config nil)
(def pubmote-save-config nil)

(def last-log-time-telemetry-tx 0)
(def last-log-time-telemetry-rx 0)

@const-end
