# Accelerometer raise-to-HBM

Raised HBM uses calibrated accelerometer samples at the existing approximately
20 Hz task rate. The QMI8658 accelerometer and gyro configuration is unchanged;
gyro measurements remain available for calibration and diagnostics, but do not
decide whether to enable HBM. HBM remains restricted to supported displays and
the Stats screen, with the existing pocket-mode and confirmation-dialog guards.

`firmware/src/imu/raise_detector.c` contains the detector and its tuning constants:

| Parameter | Initial value |
| --- | --- |
| Pickup motion filter time constant | 150 ms |
| Enter viewing pose | Current valid, normalized Z > 0.75 |
| Leave viewing pose | Current valid, normalized Z < 0.55 |
| Pose confirmation delay | None |
| HBM switch bounce suppression | 500 ms after an accepted change |
| Maximum reading window | 15 seconds |
| Pickup/lowered-pose arming window | 2.5 seconds |
| Accepted acceleration magnitude | 0.8-1.2 g |
| Sustained invalid-sample timeout | 500 ms |

A lowered pose or acceleration residual above 0.12 g arms detection. A valid
viewing pose changes the detector state immediately, with no pose dwell timer.
A static face-up remote at startup does not enable HBM. Holding the remote still
no longer classifies it as a table after three seconds. The 15-second reading
window bounds HBM usage because accelerometer data cannot reliably distinguish
a stationary hand-held remote from a table in the same orientation. After
timeout, fresh movement or lowering is needed to rearm it.

`firmware/src/imu/hbm_switch.c` suppresses bouncing at the actual HBM switch.
The first on/off request applies immediately. Opposite requests during the next
500 ms are ignored in both directions; they do not extend the window. At or after
500 ms, a different current request can apply immediately. Requests are retried
at the sensor task rate, and the UI coalesces them to the latest detector state.
An ignored opposite request is never queued to fire later: if the requested state
returns to the displayed state, nothing changes when the window expires.

For example, turning HBM on at 0 ms suppresses off requests at 100-499 ms. If the
remote is raised again by 500 ms, HBM remains on. If it remains lowered at 500 ms,
HBM turns off. The same suppression applies to on requests after turning HBM off.
This is immediate switching with bounce suppression, not a requirement to remain
in either pose for 500 ms. Existing dialog suppression, screen teardown, and
explicit setting changes can still turn HBM off immediately. Entering Stats
resets the switch guard.

Invalid sensor reads are explicitly flagged and excluded from calibration and
pose detection. A sustained invalid sample stream lowers the detector after
500 ms. Gaps longer than 250 ms restart the pickup motion filter and clear
arming; missing data cannot fabricate pickup motion.

## Validation and trace capture

Run the detector tests independently of ESP-IDF:

```sh
cmake -S tests/imu -B .pio/imu-tests
cmake --build .pio/imu-tests
ctest --test-dir .pio/imu-tests --output-on-failure
```

The main host suite includes the detector and HBM switch tests. Scenarios cover
startup on a table, pickup, steady reading, immediate pose transitions, threshold
hysteresis, timeout, invalid samples, and scheduling gaps. Switch tests cover
immediate first changes, bouncing in both directions, the exact 500-ms boundary,
returning to the displayed state, persistent latest requests, and session reset.

For hardware tuning, add `-D DEBUG_IMU=1` to the selected board's build flags and
capture the serial monitor. `raise_trace` CSV records contain monotonic ms,
calibrated acceleration X/Y/Z in g, gyro X/Y/Z in degrees/s, accelerometer and
gyro validity flags, and detector raised state. Capture each of these separately:

- Lowered-to-view raises and lowering, with different hands and grip angles.
- Picking up and placing down the remote without changing its orientation.
- Reading while holding still for more than three seconds.
- Table rest, face down, pocket mode, and confirmation-dialog suppression.
- Riding vibration, acceleration, and turning, to check false activations.

Replay a log against the compiled detector:

```sh
.pio/imu-tests/raise_trace_replay monitor.log > replay.csv
```

On Windows, use the `.exe` suffix and the generator's configuration directory
where applicable. Split monitor logs at reboot so timestamps remain monotonic.
The replay outputs recorded and recomputed states for comparison; annotate the
intended reading intervals separately to evaluate false positives and latency.
The supplied tests use synthetic samples. Hardware thresholds and riding
behavior still require validation on a device before treating them as tuned.
