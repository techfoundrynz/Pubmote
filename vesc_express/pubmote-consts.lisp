@const-start

; Pubmote protocol constants. Load before the other pubmote files.
; Everything is prefixed - LispBM has one global namespace to share with the host.

; Pairing states
(def PUBMOTE_PAIR_IDLE 0)
(def PUBMOTE_PAIR_INITIATED 1)
(def PUBMOTE_PAIR_BONDING 2)

; Vehicle types reported to the remote
(def PUBMOTE_VEHICLE_UNSPECIFIED 0)
(def PUBMOTE_VEHICLE_ONEWHEEL 1)
(def PUBMOTE_VEHICLE_ESKATE 2)
(def PUBMOTE_VEHICLE_SCOOTER 3)
(def PUBMOTE_VEHICLE_EUC 4)

; Protocol commands
(def PUBMOTE_CMD_VERSION 0)
(def PUBMOTE_CMD_VERSION_REC 5)
(def PUBMOTE_CMD_PAIR_INIT 10)
(def PUBMOTE_CMD_PAIR_BOND 11)
(def PUBMOTE_CMD_PAIR_COMPLETE 12)
(def PUBMOTE_CMD_SET_CORE_DATA 100)
(def PUBMOTE_CMD_SET_INPUT_STATE 150)
(def PUBMOTE_MAGIC 169)

; Log levels, most severe first. Error/warn/info are expected to always reach
; the console; debug is verbose and for the host to gate.
(def PUBMOTE_LOG_ERROR 0)
(def PUBMOTE_LOG_WARN 1)
(def PUBMOTE_LOG_INFO 2)
(def PUBMOTE_LOG_DEBUG 3)

@const-end
