# Charging-time estimator

Ownership: firmware power/battery maintainers; presentation and persistence
integration belong to the application.

Owns portable learning and prediction rules for approximate time to charge
completion. It depends only on standard C, with no charger, ESP-IDF, NVS, clock,
scheduler or UI dependency. Callers supply monotonic milliseconds, voltage in
mV, current in mA and charger flags through `charge_estimator_update()`.

Model and session records belong to the caller and require a single owner or
external serialization. The application keeps them private to the power task
and publishes an atomic remaining-time snapshot for the UI. The estimator
retains no global state and performs no allocation, I/O, locking or task work.

Reset the model before loading a saved record and validate it before use;
`CHARGE_ESTIMATOR_VERSION` identifies the persisted layout and learning rules.
Session state is transient and must be reset on boot. An update returns true
only when a qualifying completion changes the model, letting the application
queue persistence of a copied record. A remaining time of zero means unknown.

Learning uses filtered voltage/current ranges from uninterrupted charging
segments ending in Done. Every completion blends new history, expires unused
ranges and can replace a consistently different profile. Predictions require
matching history after a settling interval; missing or invalid telemetry,
interruptions and faults hide them. Partial-to-full charges teach only their
observed ranges.

Validation: `charge_estimator_test` in the host CMake/CTest suite covers missing
history, settling, interruptions, replacement-battery adaptation and ageing.
`python tests/test_charge_estimator.py` additionally tests the application's
persistence adapter with storage and worker scheduling stubbed. Firmware builds
and the component boundary checker verify integration. Hardware charge cycles
are needed to assess estimate accuracy.
