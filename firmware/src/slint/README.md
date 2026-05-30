# PubRemote Slint UI

Slint ports of the existing LVGL/SquareLine screens. **Markup only** — these
`.slint` files define the UI; they are not yet wired into the firmware build
(that integration was prototyped separately and reverted). Compile
`app-window.slint` with the Slint compiler to generate `app-window.h`, which
exposes the `AppWindow` component and the `UiState` global to C++.

## Layout

```
slint/
  app-window.slint     Top-level window: screen switcher + UiState global (the
                       single binding surface for C++). Imports everything else.
  ui/
    theme.slint        Theme global: palette, font sizes, spacing (matches LVGL).
    widgets.slint      Reusable AppButton / IconButton / ProgressBar.
    splash.slint       SplashScreen      (boot logo)
    stats.slint        StatsScreen       (main riding display)
    menu.slint         MenuScreen        (Settings / Calibration / About)
    settings.slint     SettingsScreen    (data-driven [SettingRow] list)
    pairing.slint      PairingScreen     (ESP-NOW pairing flow)
    calibration.slint  CalibrationScreen (joystick wizard + live dot)
    about.slint        AboutScreen       (version / build / hardware)
    update.slint       UpdateScreen      (OTA progress)
    charge.slint       ChargeScreen      (on-charger display)
```

## Driving the UI from C++

Everything dynamic flows through the **`UiState`** global (exported from
`app-window.slint`). After `auto ui = AppWindow::create();`:

```cpp
auto state = ui->global<UiState>();

// Switch screens:
state.set_screen(Screen::Stats);

// Push data (called whenever values change):
state.set_speed("23");
state.set_battery_percent(87);
state.set_status("Connected");
state.set_connected(true);

// Handle actions:
state.on_open_settings([&] { state.set_screen(Screen::Settings); });
state.on_pairing_action([&] { /* start/stop bind */ });
```

Hyphenated Slint names map to snake_case in C++ (`battery-percent` →
`set_battery_percent`, `splash-tapped` → `on_splash_tapped`).

### Settings list

`SettingsScreen` is data-driven. Build a `SettingRow` model in C++ and set it:

```cpp
auto rows = std::make_shared<slint::VectorModel<SettingRow>>();
rows->push_back({ .name = "Units", .value = "Metric" });
state.set_settings_rows(rows);
state.on_settings_row_activated([&](int i) { /* open editor / cycle value */ });
```

## Known gaps / TODO

- **Icons & logo**: the LVGL UI used a symbol font (battery/footpad glyphs) and
  `ui_img_icon2_png`. These are approximated with shapes/text. Embed real assets
  via `@image-url` / an embedded font when available.
- **Fonts**: sizes follow the Inter Bold scale (96/48/28/14) but use the default
  family; embed Inter (`import "Inter.ttf";`) for exact glyphs.
- **Stats dial**: the LVGL stats screen had a duty/throttle dial arc; not yet
  ported (the core speed/battery/status/footpad layout is).
- **Not build-validated**: no Slint compiler was available in this environment,
  so these haven't been compiled. Run `slint-compiler app-window.slint -f cpp`
  to check before wiring into the firmware.
```
