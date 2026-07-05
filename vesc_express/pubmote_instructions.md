# Pubmote Library Usage Instructions

The `pubmote.lisp` file is designed to be imported and used as a standalone library by other VESC Lisp packages.

## Integration Steps

1. **Import and load the library** in your host script:
   ```lisp
   (import "lib/pubmote.lisp" 'pubmote)
   (read-eval-program pubmote)
   ```

2. **Initialize callbacks and vehicle mode** using `setup-pubmote`:
   ```lisp
   (setup-pubmote
       vehicle-type     ; Vehicle type (e.g., (assoc pubmote-vehicle-types VEHICLE_TYPE_ONEWHEEL))
       on-control-cb    ; Callback: (fn (jsy jsx bt-c bt-z is-rev) { ... })
       get-telemetry-cb ; Callback returning telemetry list: (fn () { (list fault-code pitch roll ... ) })
       send-msg-cb      ; Callback for logging: (fn (text) { ... })
       get-config-cb    ; Callback to get config value: (fn (name) { ... })
       set-config-cb    ; Callback to set config value: (fn (name val) { ... })
       save-config-cb   ; Callback to persist config to eeprom: (fn () { ... })
   )
   ```

3. **Spawn the background loop** thread:
   ```lisp
   (setq pubmote-context-id (spawn pubmote-loop))
   ```

## Supported Vehicle Types
Vehicle types are defined in the `pubmote-vehicle-types` association list:
- `VEHICLE_TYPE_UNSPECIFIED` (0)
- `VEHICLE_TYPE_ONEWHEEL` (1)
- `VEHICLE_TYPE_ESKATE` (2)
- `VEHICLE_TYPE_SCOOTER` (3)
- `VEHICLE_TYPE_EUC` (4)
