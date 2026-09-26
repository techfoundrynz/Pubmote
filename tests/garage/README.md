# Garage Rider

Open **About**, tap the version five times to unlock the arcade, then choose **Garage Rider**.
Mario from TheBoardGarage rides an original pixel-art Onewheel through three increasingly fast trails.

- Tap the playfield or press the physical button to jump. A tap also starts, retries, or advances between trails.
- The rider rolls forward automatically. The on-screen arrows or stick left/right change direction.
- **Stop** or stick down brakes. Stick up also jumps.
- Park Hops, Rooftop Run, and Bot Alley have distinct layouts and shorter finishes (360, 460, and 560 world units). The HUD shows progress toward each garage.
- Collect batteries for 10 points. Pick up another within 2.5 seconds to build a multiplier up to x4. Land on patrol bots for 25 points.
- Five batteries in a trail restore one life up to a maximum of three (or award 50 points at full lives). Each finish earns a star, plus one for five batteries and one for no crashes; bonus stars add 50 points each to the 100-point finish. Aim for 9/9 across the ride.
- Falling into a pit or hitting a bot costs a life. The flag saves a checkpoint for that trail.
- Speed starts at 40 world units/s, grows by 0.45 per second of active play (up to +20), and rises by +6 per lap and +2 per trail, capped at 76. Turbo adds another 8. Patrols also speed up. The HUD shows lap, speed, progress and battery goal.
- Completing three trails starts a harder lap, preserving score, lives and elapsed difficulty. Only game over or leaving starts a fresh run. Later laps add ground patrols where there is room and up to five aerial patrols; enemy speed caps at 36.
- **S / cyan shield:** smash bots for four seconds. **M / purple magnet:** collect nearby batteries for eight seconds. **T / orange turbo:** extra speed and bot-smashing for 2.5 seconds. Timers pause between stages; effects clear on respawn. Pickups do not respawn after a crash. Pits still kill while powered up.
- Jumps take about 0.78 seconds, with a quick launch and a sharper descent.
- An original goofy chiptune loops while this screen is open. Sound effects interrupt it; the next phrase resumes afterward. Sound follows the device sound setting and stops on exit.
- Start with three lives across all three trails; battery rewards can replenish them. Collected batteries and defeated bots stay collected after a crash.
- The best score is saved when a ride crashes or a trail finishes. **Exit** returns to the arcade.

The fixed-step C++ game core is independent of ESP-IDF and Slint.
It uses bounded storage and caps catch-up time after slow frames.
Only visible objects are published to the UI model.

## Host tests

With CMake and a C++17 compiler:

```sh
cmake -S tests/garage -B .pio/garage-host
cmake --build .pio/garage-host --config Debug
ctest --test-dir .pio/garage-host -C Debug --output-on-failure
```

Tests exercise collisions, jumping, pickups, stomping, death, checkpoint respawn, restart, and bounded time steps.
Terrain-only traversal tests use braking at normal and maximum pace. Combat, power-up effects/expiry, lap persistence, increasing enemy counts, storage limits, and the life cap are checked separately. Full combat balance still needs hands-on device play.
Timing checks verify jump height, airtime, the speed cap, and that menus do not advance difficulty.

For hardware validation, build a supported PlatformIO environment and check touch, button, stick, sound, high-score persistence, and exit/re-entry on the device.
The UI is designed in normalized coordinates for both 240-pixel and 466-pixel panels.
