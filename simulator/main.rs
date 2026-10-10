//! Desktop service doubles bound to the firmware's unmodified Slint UI.
use slint::{Model, ModelRc, Timer, TimerMode, VecModel};
use slint_interpreter::{
    Brush, Color, Compiler, ComponentHandle, ComponentInstance, Image, Rgb8Pixel,
    SharedPixelBuffer, Struct, Value,
};
use std::{
    cell::{Cell, RefCell},
    path::Path,
    time::Duration,
};
use web_time::Instant;

thread_local! {
    static LAST_KEY: RefCell<Option<(usize, usize, Instant)>> = const { RefCell::new(None) };
    static BASELINE: Cell<bool> = const { Cell::new(false) };
}

fn keyboard_mode(ui: &ComponentInstance, mode: &str) {
    let keys = match mode {
        "abc" => [
            ".,?!", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz", " ", "0", "Del",
        ],
        "ABC" => [
            ".,?!", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ", " ", "0", "Del",
        ],
        "#+=" => [
            "!@#", "$%&", "()", "-_", "+=", "/\\", ":;", "\"'", "<>?", " ", "*[]{}", "Del",
        ],
        _ => ["1", "2", "3", "4", "5", "6", "7", "8", "9", " ", "0", "Del"],
    };
    set(ui, "wifi-mode-label", text(mode));
    set(ui, "wifi-keys", strings(&keys));
    LAST_KEY.with(|last| *last.borrow_mut() = None);
}

fn text(s: &str) -> Value {
    Value::String(s.into())
}
fn list(values: Vec<Value>) -> Value {
    Value::Model(ModelRc::new(VecModel::from(values)))
}
fn strings(values: &[&str]) -> Value {
    list(values.iter().map(|s| text(s)).collect())
}
fn record(fields: &[(&str, Value)]) -> Value {
    let mut value = Struct::default();
    for (name, v) in fields {
        value.set_field(name.replace('-', "_"), v.clone());
    }
    Value::Struct(value)
}
fn set(ui: &ComponentInstance, name: &str, value: impl Into<Value>) {
    update_global(ui, "UiState", name, value.into());
}
pub(crate) fn get(ui: &ComponentInstance, name: &str) -> Value {
    match ui.get_global_property("UiState", name) {
        Ok(value) => value,
        Err(_) if BASELINE.get() => Value::Void,
        Err(error) => panic!("UiState.{name}: {error:?}"),
    }
}
fn update_global(ui: &ComponentInstance, global: &str, name: &str, value: Value) {
    if let Err(error) = ui.set_global_property(global, name, value) {
        if BASELINE.get() {
            eprintln!("Base fixture skipped {global}.{name}: {error:?}");
        } else {
            panic!("{global}.{name}: {error:?}");
        }
    }
}
fn number(v: Value) -> f64 {
    if let Value::Number(n) = v { n } else { 0.0 }
}
fn boolean(v: Value) -> bool {
    matches!(v, Value::Bool(true))
}
fn string(v: Value) -> String {
    if let Value::String(s) = v {
        s.to_string()
    } else {
        String::new()
    }
}
pub(crate) fn screen(ui: &ComponentInstance, name: &str) {
    set(
        ui,
        "screen",
        Value::EnumerationValue("Screen".into(), name.replace('_', "-")),
    );
}
fn theme(ui: &ComponentInstance, name: &str, value: impl Into<Value>) {
    update_global(ui, "Theme", name, value.into());
}
fn hsv(h: f64, s: f64, v: f64) -> Color {
    let c = v * s;
    let h = h.rem_euclid(360.0) / 60.0;
    let x = c * (1.0 - (h % 2.0 - 1.0).abs());
    let (r, g, b) = match h as u8 {
        0 => (c, x, 0.0),
        1 => (x, c, 0.0),
        2 => (0.0, c, x),
        3 => (0.0, x, c),
        4 => (x, 0.0, c),
        _ => (c, 0.0, x),
    };
    let m = v - c;
    Color::from_rgb_u8(
        ((r + m) * 255.0) as u8,
        ((g + m) * 255.0) as u8,
        ((b + m) * 255.0) as u8,
    )
}
fn apply_settings(ui: &ComponentInstance) {
    let color = hsv(
        number(get(ui, "theme-h")),
        number(get(ui, "theme-s")) / 100.0,
        number(get(ui, "theme-l")) / 100.0,
    );
    theme(ui, "accent", Value::Brush(Brush::SolidColor(color)));
    theme(
        ui,
        "primary-button-text",
        Value::Brush(Brush::SolidColor(
            if 0.299 * color.red() as f64
                + 0.587 * color.green() as f64
                + 0.114 * color.blue() as f64
                > 140.0
            {
                Color::from_rgb_u8(0, 0, 0)
            } else {
                Color::from_rgb_u8(255, 255, 255)
            },
        )),
    );
}
fn boards(ui: &ComponentInstance, paired: bool) {
    set(
        ui,
        "paired-boards",
        list(if paired {
            vec![record(&[
                ("mac", text("02:00:00:00:00:01")),
                ("index", 0.into()),
                ("is-active", true.into()),
                ("is-ble", false.into()),
            ])]
        } else {
            vec![]
        }),
    );
    set(ui, "menu-show-connect", paired);
}
fn wifi_scan(ui: &ComponentInstance) {
    set(
        ui,
        "wifi-networks",
        list(
            ["Simulator Wi-Fi", "Workshop"]
                .iter()
                .map(|ssid| {
                    record(&[
                        ("ssid", text(ssid)),
                        ("detail", text("Simulated • secured • -45 dBm")),
                    ])
                })
                .collect(),
        ),
    );
    set(ui, "wifi-status", text("Two simulated networks found"));
}
fn edit_wifi(ui: &ComponentInstance, secret: bool) {
    set(ui, "wifi-editing", true);
    set(ui, "wifi-secret", secret);
    set(
        ui,
        "wifi-heading",
        text(if secret { "PASSWORD" } else { "NETWORK NAME" }),
    );
    set(ui, "wifi-value", text(""));
    set(ui, "wifi-masked-value", text(""));
    keyboard_mode(ui, "abc");
}

/// Every service action is explicit; an added firmware callback fails the smoke test
/// instead of silently becoming an inert button.
fn handle(ui: &ComponentInstance, controls: &ComponentInstance, name: &str, args: &[Value]) {
    match name {
        "screen-changed" => {}
        // The controls window supplies charge telemetry; screen selection is manual.
        "charge-poll" => {
            set(ui, "charge-percent", controls.get_property("remote-battery").unwrap());
            set(
                ui,
                "charge-label",
                text(if boolean(controls.get_property("charging").unwrap()) {
                    "Charging"
                } else {
                    "Power connected"
                }),
            );
        }
        "charge-tapped" => {
            if matches!(get(ui, "screen"), Value::EnumerationValue(_, name) if name == "charge") {
                screen(ui, "stats");
            }
        }
        "splash-tapped" | "menu-back" => screen(ui, "stats"),
        "stats-swiped-down"
        | "about-back"
        | "boards-back"
        | "wifi-back"
        | "input-calibration-secondary"
        | "imu-calibration-back" => screen(ui, "menu"),
        "open-settings" => screen(ui, "settings"),
        "open-input-calibration" => screen(ui, "input-calibration"),
        "open-pairing" => screen(ui, "boards"),
        "open-about" => screen(ui, "about"),
        "open-wifi" => {
            wifi_scan(ui);
            screen(ui, "wifi");
        }
        "open-imu-calibration" => screen(ui, "imu-calibration"),
        "open-games" => screen(ui, "games"),
        "games-back" => screen(ui, "about"),
        "game-back" => screen(ui, "games"),
        "menu-connect" => {
            let connected = !boolean(controls.get_property("connected").unwrap());
            controls
                .set_property("connected", connected.into())
                .unwrap();
            screen(ui, "stats");
        }
        "menu-pocket-mode" => set(
            ui,
            "pocket-mode-active",
            !boolean(get(ui, "pocket-mode-active")),
        ),
        "menu-toggle-hbm" => {
            set(
                ui,
                "hbm-mode-label",
                text(if string(get(ui, "hbm-mode-label")) == "Off" {
                    "On"
                } else {
                    "Off"
                }),
            );
        }
        "menu-toggle-led" => {
            set(
                ui,
                "led-mode-label",
                text(if string(get(ui, "led-mode-label")) == "Off" {
                    "On"
                } else {
                    "Off"
                }),
            );
        }
        "menu-shutdown-long-press" => set(ui, "shutdown-text", text("Factory reset?")),
        "menu-shutdown" => {
            set(ui, "confirm-dialog-title", text("Simulated power off"));
            set(
                ui,
                "confirm-dialog-message",
                text("Return to the splash screen? Settings remain in this session."),
            );
            set(ui, "show-confirm-dialog", true);
        }
        "confirm-dialog-accepted" => {
            set(ui, "show-confirm-dialog", false);
            screen(ui, "splash");
        }
        "confirm-dialog-rejected" => set(ui, "show-confirm-dialog", false),
        "settings-changed" => apply_settings(ui),
        "settings-save" => {
            apply_settings(ui);
            screen(ui, "menu");
        }
        "secondary-stat-left-clicked" => {
            let duty = string(get(ui, "secondary-stat-left-label")) != "Duty";
            set(
                ui,
                "secondary-stat-left-label",
                text(if duty { "Duty" } else { "Temp" }),
            );
        }
        "board-battery-clicked" => {
            let voltage = string(get(ui, "board-battery")).ends_with('%');
            set(
                ui,
                "board-battery",
                text(if voltage { "78.4V" } else { "82%" }),
            );
        }
        "delete-board" => boards(ui, false),
        "select-board" => {
            boards(ui, true);
            controls.set_property("connected", true.into()).unwrap();
            screen(ui, "stats");
        }
        "boards-pair-new" => {
            set(ui, "pairing-code", text("1234"));
            set(ui, "pairing-status", text("Simulated board ready to pair"));
            set(ui, "pairing-action-text", text("Pair"));
            screen(ui, "pairing");
        }
        "pairing-action" | "select-device" => {
            boards(ui, true);
            screen(ui, "boards");
        }
        "retry-ble-scan" => set(
            ui,
            "discovered-devices",
            list(vec![record(&[
                ("name", text("Simulated VESC")),
                ("index", 0.into()),
            ])]),
        ),
        "input-calibration-primary" => {
            let active = !boolean(get(ui, "show-expo-slider"));
            set(ui, "show-expo-slider", active);
            set(ui, "show-invert-switch", active);
            set(
                ui,
                "input-calibration-primary-text",
                text(if active { "Save" } else { "Start" }),
            );
            set(
                ui,
                "input-calibration-step",
                text(if active {
                    "Move the simulated joystick sliders"
                } else {
                    "Calibration saved for this session"
                }),
            );
        }
        "imu-calibration-primary" => {
            let enabled = !boolean(get(ui, "imu-calibration-enabled"));
            set(ui, "imu-calibration-enabled", enabled);
            set(ui, "imu-calibration-show-axis-controls", enabled);
            set(
                ui,
                "imu-calibration-step-text",
                text(if enabled {
                    "Simulated sensors active; press Next to finish"
                } else {
                    "Calibration complete"
                }),
            );
        }
        "imu-calibration-calibrate-level" => set(
            ui,
            "imu-calibration-event-text",
            text("Simulated level saved"),
        ),
        "imu-calibration-toggle-invert-x" => set(
            ui,
            "imu-calibration-invert-x",
            !boolean(get(ui, "imu-calibration-invert-x")),
        ),
        "imu-calibration-toggle-invert-y" => set(
            ui,
            "imu-calibration-invert-y",
            !boolean(get(ui, "imu-calibration-invert-y")),
        ),
        "imu-calibration-toggle-swap-xy" => set(
            ui,
            "imu-calibration-swap-xy",
            !boolean(get(ui, "imu-calibration-swap-xy")),
        ),
        "about-refresh" => {}
        "about-check-updates" => {
            set(ui, "updates", strings(&["Simulator firmware"]));
            set(ui, "update-show-dropdown", true);
            set(ui, "update-body", text("A simulated update is available"));
            set(ui, "update-primary-text", text("Install"));
            screen(ui, "update");
        }
        "update-primary" => {
            set(
                ui,
                "update-body",
                text("Simulated update completed. No firmware was downloaded or flashed."),
            );
            set(ui, "update-primary-text", text("Done"));
        }
        "update-secondary" => screen(ui, "about"),
        "update-selected" => {}
        "wifi-scan" => wifi_scan(ui),
        "wifi-select" => {
            set(
                ui,
                "wifi-saved",
                text(
                    if number(args.first().cloned().unwrap_or_default()) == 0.0 {
                        "Simulator Wi-Fi"
                    } else {
                        "Workshop"
                    },
                ),
            );
            edit_wifi(ui, true);
        }
        "wifi-manual" => edit_wifi(ui, false),
        "wifi-forget" => {
            set(ui, "wifi-saved", text(""));
            set(ui, "wifi-status", text("Simulated network forgotten"));
        }
        "wifi-connect-saved" => set(ui, "wifi-status", text("Connected to simulated Wi-Fi")),
        "wifi-mode" => {
            let mode = string(get(ui, "wifi-mode-label"));
            keyboard_mode(
                ui,
                match mode.as_str() {
                    "abc" => "ABC",
                    "ABC" => "123",
                    "123" => "#+=",
                    _ => "abc",
                },
            );
        }
        "wifi-key" => {
            let index = number(args.first().cloned().unwrap_or_default()) as usize;
            let mut value = string(get(ui, "wifi-value"));
            if index == 11 {
                value.pop();
                LAST_KEY.with(|last| *last.borrow_mut() = None);
            } else if let Value::Model(model) = get(ui, "wifi-keys") {
                if let Some(key) = model.row_data(index) {
                    let chars: Vec<_> = string(key).chars().collect();
                    LAST_KEY.with(|last| {
                        let mut last = last.borrow_mut();
                        let cycle = if let Some((previous, cycle, time)) = *last {
                            if previous == index
                                && chars.len() > 1
                                && time.elapsed() < Duration::from_millis(800)
                            {
                                value.pop();
                                (cycle + 1) % chars.len()
                            } else {
                                0
                            }
                        } else {
                            0
                        };
                        if let Some(ch) = chars.get(cycle) {
                            if value.chars().count()
                                < if boolean(get(ui, "wifi-secret")) {
                                    64
                                } else {
                                    32
                                }
                            {
                                value.push(*ch);
                                *last = Some((index, cycle, Instant::now()));
                            }
                        }
                    });
                }
            }
            set(ui, "wifi-value", text(&value));
            set(
                ui,
                "wifi-masked-value",
                text(&"*".repeat(value.chars().count())),
            );
        }
        "wifi-done" => {
            if !boolean(get(ui, "wifi-secret")) {
                set(ui, "wifi-saved", get(ui, "wifi-value"));
                edit_wifi(ui, true);
            } else {
                set(ui, "wifi-editing", false);
                set(ui, "wifi-status", text("Connected to simulated Wi-Fi"));
            }
        }
        "wifi-cancel" => set(ui, "wifi-editing", false),
        "games-launch" => {
            set(
                ui,
                "game-error",
                text("Use scripts/play_game.py for the desktop Lua game runtime"),
            );
            screen(ui, "game");
        }
        "game-tick" | "game-event" => {}
        _ => panic!("Simulator has no handler for UiState.{name}"),
    }
}

pub(crate) fn initialize(
    ui: &ComponentInstance,
    controls: &ComponentInstance,
    width: f64,
    height: f64,
    square: bool,
) {
    theme(ui, "panel-res", width.min(height));
    theme(ui, "panel-width", width);
    theme(ui, "panel-height", height);
    theme(ui, "is-square-mode", square);
    for name in [
        "imu-supported",
        "joystick-supported",
        "joystick-x-supported",
        "joystick-y-supported",
        "button-supported",
        "hbm-mode-supported",
        "led-mode-supported",
    ] {
        set(ui, name, true);
    }
    for (name, value) in [
        ("hbm-mode-label", "Off"),
        ("led-mode-label", "Off"),
        ("speed-unit", "KPH"),
        ("secondary-stat-left-label", "Temp"),
        ("secondary-stat-left-value", "32°C"),
        (
            "version-info",
            "Version: simulator\nType: desktop\nHardware: simulated",
        ),
        (
            "imu-calibration-step-text",
            "Press Next to begin simulated calibration",
        ),
    ] {
        set(ui, name, text(value));
    }
    set(ui, "vehicle-type", 1);
    set(ui, "theme-h", 207);
    set(ui, "theme-s", 86);
    set(ui, "theme-l", 95);
    for (name, values) in [
        ("double-press-options", vec!["Open menu", "None"]),
        ("rotation-options", vec!["0°", "90°", "180°", "270°"]),
        (
            "auto-off-options",
            vec!["Never", "1 minute", "5 minutes", "10 minutes"],
        ),
        ("temp-units-options", vec!["Celsius", "Fahrenheit"]),
        ("distance-units-options", vec!["Kilometres", "Miles"]),
        ("startup-sound-options", vec!["Off", "On"]),
    ] {
        set(ui, name, strings(&values));
    }
    set(
        ui,
        "about-stats",
        list(vec![record(&[
            ("label", text("Backend")),
            ("value", text("Desktop simulator")),
            ("is-header", false.into()),
        ])]),
    );
    boards(ui, true);
    wifi_scan(ui);
    apply_settings(ui);
    let weak = ui.as_weak();
    let cweak = controls.as_weak();
    let callbacks: Vec<_> = ui
        .definition()
        .global_callbacks("UiState")
        .unwrap()
        .collect();
    for name in callbacks {
        let weak = weak.clone();
        let cweak = cweak.clone();
        let callback = name.clone();
        ui.set_global_callback("UiState", &name, move |args| {
            if let (Some(ui), Some(controls)) = (weak.upgrade(), cweak.upgrade()) {
                handle(&ui, &controls, &callback, args);
            }
            Value::Void
        })
        .unwrap();
    }
    let gradient = ui.set_global_callback("ColorSliderGenerator", "generate-track", |args| {
        let n = |i: usize| number(args[i].clone());
        let width = n(0).clamp(1.0, 1200.0) as u32;
        let height = n(1).clamp(1.0, 1200.0) as u32;
        let mut pixels = SharedPixelBuffer::<Rgb8Pixel>::new(width, height);
        for (i, pixel) in pixels.make_mut_slice().iter_mut().enumerate() {
            let fraction = (i as u32 % width) as f64 / width.saturating_sub(1).max(1) as f64;
            let color = hsv(
                if n(2) == 0.0 { fraction * 360.0 } else { n(3) },
                if n(2) == 1.0 { fraction } else { n(4) / 100.0 },
                if n(2) == 2.0 { fraction } else { n(5) / 100.0 },
            );
            *pixel = Rgb8Pixel::new(color.red(), color.green(), color.blue());
        }
        Value::Image(Image::from_rgb8(pixels))
    });
    if let Err(error) = gradient {
        if !BASELINE.get() {
            panic!("ColorSliderGenerator: {error:?}");
        }
    }
    let weak = ui.as_weak();
    controls
        .set_callback("navigate", move |args| {
            if let Some(ui) = weak.upgrade() {
                screen(&ui, &string(args[0].clone()));
            }
            Value::Void
        })
        .unwrap();
}

fn speed_text(speed: f64, miles: bool) -> String {
    // Match stats_screen.cpp: snap signed zero and drop the decimal once the
    // reading rounds to 10. The device deliberately limits readout width.
    let converted = speed as f32 * if miles { 0.621371_f32 } else { 1.0 };
    let converted = if converted.abs() < 0.05 { 0.0 } else { converted };
    if (converted * 10.0).round() / 10.0 >= 10.0 {
        format!("{converted:.0}")
    } else {
        format!("{converted:.1}")
    }
}

pub(crate) fn tick(ui: &ComponentInstance, controls: &ComponentInstance, seconds: f64) {
    let prop = |name| controls.get_property(name).unwrap();
    let connected = boolean(prop("connected"));
    let speed = if !connected {
        0.0
    } else if boolean(prop("animate")) {
        18.0 + 12.0 * (seconds * 0.3).sin()
    } else {
        number(prop("speed"))
    };
    let miles = number(get(ui, "distance-units-index")) == 1.0;
    set(ui, "speed", text(&speed_text(speed, miles)));
    set(ui, "speed-unit", text(if miles { "MPH" } else { "KPH" }));
    set(ui, "speed-fraction", (speed / 60.0).clamp(0.0, 1.0));
    set(ui, "duty-fraction", (speed / 65.0).clamp(0.0, 1.0));
    set(ui, "is-connected", connected);
    set(ui, "connection-state", if connected { 2 } else { 0 });
    set(
        ui,
        "connection-state-label",
        text(if connected {
            "Connected"
        } else {
            "Disconnected"
        }),
    );
    set(
        ui,
        "menu-connect-label",
        text(if connected { "Disconnect" } else { "Connect" }),
    );
    for name in ["left-pad", "right-pad", "remote-battery", "rssi"] {
        set(ui, name, prop(name));
    }
    set(ui, "remote-charging", prop("charging"));
    set(ui, "charge-percent", prop("remote-battery"));
    if string(get(ui, "board-battery")).ends_with('V') {
        set(
            ui,
            "board-battery",
            text(&format!("{:.1}V", 60.0 + number(prop("battery")) * 0.24)),
        );
    } else {
        set(
            ui,
            "board-battery",
            text(&format!("{:.0}%", number(prop("battery")))),
        );
    }
    let x = number(prop("joystick-x"));
    let y = number(prop("joystick-y"));
    set(ui, "joystick-x", x);
    set(ui, "joystick-y", y);
    set(
        ui,
        "input-calibration-readout",
        text(&format!("X: {x:.2} | Y: {y:.2}")),
    );
    set(ui, "imu-calibration-accel-x", x);
    set(ui, "imu-calibration-accel-y", y);
    set(ui, "imu-calibration-accel-z", 1.0);
    set(
        ui,
        "imu-calibration-accel-text",
        text(&format!("A: X={x:.2} Y={y:.2} Z=1.00")),
    );
    let duty = string(get(ui, "secondary-stat-left-label")) == "Duty";
    set(
        ui,
        "secondary-stat-left-value",
        text(&if duty {
            format!("{:.0}%", speed / 65.0 * 100.0)
        } else if number(get(ui, "temp-units-index")) == 1.0 {
            "90°F".into()
        } else {
            "32°C".into()
        }),
    );
}

#[cfg(not(target_arch = "wasm32"))]
fn load(path: &Path, component: &str) -> Result<ComponentInstance, Box<dyn std::error::Error>> {
    let compiler = Compiler::default();
    let result = spin_on::spin_on(compiler.build_from_path(path));
    for diagnostic in result.diagnostics() {
        eprintln!("{diagnostic}");
    }
    Ok(result
        .component(component)
        .ok_or("UI compilation failed")?
        .create()?)
}
#[cfg(not(target_arch = "wasm32"))]
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args().collect();
    let option = |key: &str, fallback: &str| {
        args.windows(2)
            .find(|a| a[0] == key)
            .map(|a| a[1].clone())
            .unwrap_or(fallback.into())
    };
    let root = option("--root", ".");
    let root = Path::new(&root);
    let ui_root = option("--ui-root", &root.to_string_lossy());
    let ui_root = Path::new(&ui_root);
    let smoke = args.iter().any(|s| s == "--smoke-test");
    let capture = args.iter().any(|s| s == "--capture");
    if smoke || capture {
        slint::platform::set_platform(Box::new(i_slint_backend_testing::TestingBackend::new(
            i_slint_backend_testing::TestingBackendOptions {
                mock_time: true,
                threading: false,
                renderer_name: Some("software".into()),
            },
        )))?;
    }
    if capture {
        BASELINE.set(args.iter().any(|s| s == "--baseline"));
        capture_scenarios(
            root,
            ui_root,
            Path::new(&option("--snapshot-dir", ".pio/simulator/snapshots")),
            option("--width", "466").parse()?,
            option("--height", "466").parse()?,
            args.iter().any(|s| s == "--square"),
        )?;
        return Ok(());
    }
    let ui = load(
        &ui_root.join("firmware/src/slint/app-window.slint"),
        "AppWindow",
    )?;
    let controls = load(&root.join("simulator/controls.slint"), "SimulatorControls")?;
    initialize(
        &ui,
        &controls,
        option("--width", "466").parse()?,
        option("--height", "466").parse()?,
        args.iter().any(|s| s == "--square"),
    );
    tick(&ui, &controls, 0.0);
    if smoke {
        // Invoke each real exported callback to catch UI/simulator contract drift.
        let callbacks: Vec<_> = ui
            .definition()
            .global_callbacks("UiState")
            .unwrap()
            .collect();
        for name in &callbacks {
            ui.invoke_global(
                "UiState",
                name,
                &match name.as_str() {
                    "screen-changed" => {
                        vec![Value::EnumerationValue("Screen".into(), "stats".into())]
                    }
                    "game-event" => vec![0.into(), 0.into(), 0.into()],
                    "select-board" | "delete-board" | "select-device" | "games-launch"
                    | "update-selected" | "wifi-select" | "wifi-key" => vec![0.into()],
                    _ => vec![],
                },
            )
            .unwrap();
        }
        controls.set_property("remote-battery", 73.into())?;
        controls.set_property("charging", true.into())?;
        screen(&ui, "charge");
        ui.invoke_global("UiState", "charge-poll", &[])?;
        assert_eq!(number(get(&ui, "charge-percent")), 73.0);
        assert_eq!(string(get(&ui, "charge-label")), "Charging");
        ui.invoke_global("UiState", "charge-tapped", &[])?;
        assert!(matches!(get(&ui, "screen"), Value::EnumerationValue(_, name) if name == "stats"));
        controls.set_property("charging", false.into())?;
        ui.invoke_global("UiState", "charge-poll", &[])?;
        assert_eq!(string(get(&ui, "charge-label")), "Power connected");
        controls.set_property("connected", false.into())?;
        tick(&ui, &controls, 0.0);
        assert!(!boolean(get(&ui, "is-connected")));
        assert_eq!(string(get(&ui, "speed")), "0.0");
        ui.invoke_global("UiState", "menu-connect", &[])?;
        tick(&ui, &controls, 0.0);
        assert!(boolean(get(&ui, "is-connected")));
        controls.set_property("animate", false.into())?;
        controls.set_property("speed", 40.into())?;
        set(&ui, "distance-units-index", 1);
        tick(&ui, &controls, 0.0);
        assert_eq!(string(get(&ui, "speed")), "25");
        for (speed, expected) in [(0.0, "0.0"), (-0.01, "0.0"), (8.8, "8.8"),
                                  (9.94, "9.9"), (9.95, "10"), (28.0, "28"), (28.8, "29")] {
            assert_eq!(speed_text(speed, false), expected);
        }
        ui.invoke_global("UiState", "wifi-manual", &[])?;
        ui.invoke_global("UiState", "wifi-key", &[1.into()])?;
        ui.invoke_global("UiState", "wifi-key", &[1.into()])?;
        assert_eq!(string(get(&ui, "wifi-value")), "b");
        ui.invoke_global("UiState", "wifi-key", &[2.into()])?;
        assert_eq!(string(get(&ui, "wifi-value")), "bd");
        ui.invoke_global("UiState", "wifi-key", &[11.into()])?;
        assert_eq!(string(get(&ui, "wifi-value")), "b");
        ui.invoke_global("UiState", "wifi-done", &[])?;
        assert_eq!(string(get(&ui, "wifi-saved")), "b");
        assert!(boolean(get(&ui, "wifi-secret")));
        assert_eq!(string(get(&ui, "wifi-value")), "");
        let snapshots = root.join(".pio/simulator/snapshots");
        std::fs::create_dir_all(&snapshots)?;
        ui.show()?;
        for name in [
            "stats",
            "menu",
            "settings",
            "boards",
            "pairing",
            "wifi",
            "input-calibration",
            "imu-calibration",
            "about",
            "update",
            "charge",
            "games",
        ] {
            screen(&ui, name);
            i_slint_backend_testing::mock_elapsed_time(Duration::from_millis(600));
            save_snapshot(&ui, &snapshots.join(format!("{name}.png")))?;
        }
        controls.show()?;
        save_snapshot(&controls, &snapshots.join("controls.png"))?;
        println!(
            "Simulator smoke test passed: {} callbacks, telemetry, connection, units, keyboard and screen rendering",
            callbacks.len()
        );
        return Ok(());
    }
    let timer = Timer::default();
    let weak = ui.as_weak();
    let cweak = controls.as_weak();
    let start = Instant::now();
    timer.start(TimerMode::Repeated, Duration::from_millis(50), move || {
        if let (Some(ui), Some(controls)) = (weak.upgrade(), cweak.upgrade()) {
            tick(&ui, &controls, start.elapsed().as_secs_f64());
        }
    });
    ui.show()?;
    controls.show()?;
    slint_interpreter::run_event_loop()?;
    Ok(())
}

/// Fresh instances for each fixture prevent callback order and preceding screens
/// from changing screenshots. The same harness renders base and PR UI sources.
#[cfg(not(target_arch = "wasm32"))]
fn capture_scenarios(
    root: &Path,
    ui_root: &Path,
    output: &Path,
    width: f64,
    height: f64,
    square: bool,
) -> Result<(), Box<dyn std::error::Error>> {
    std::fs::create_dir_all(output)?;
    for name in [
        "stats",
        "stats-disconnected",
        "stats-low-battery",
        "menu",
        "settings",
        "boards",
        "boards-empty",
        "pairing",
        "wifi",
        "wifi-keyboard",
        "input-calibration",
        "imu-calibration",
        "about",
        "update",
        "charge",
        "charge-full",
        "games",
        "shutdown-dialog",
    ] {
        let ui = load(
            &ui_root.join("firmware/src/slint/app-window.slint"),
            "AppWindow",
        )?;
        let controls = load(&root.join("simulator/controls.slint"), "SimulatorControls")?;
        initialize(&ui, &controls, width, height, square);
        controls.set_property("animate", false.into())?;
        controls.set_property("speed", 28.into())?;
        if name == "stats-disconnected" {
            controls.set_property("connected", false.into())?;
        }
        if name == "stats-low-battery" {
            controls.set_property("battery", 8.into())?;
            controls.set_property("remote-battery", 12.into())?;
            controls.set_property("rssi", (-92).into())?;
        }
        if name == "charge-full" {
            controls.set_property("remote-battery", 100.into())?;
        }
        tick(&ui, &controls, 0.0);
        let target = match name {
            "stats-disconnected" | "stats-low-battery" => "stats",
            "charge-full" => "charge",
            "boards-empty" => {
                boards(&ui, false);
                "boards"
            }
            "wifi-keyboard" => {
                edit_wifi(&ui, true);
                set(&ui, "wifi-value", text("secret"));
                set(&ui, "wifi-masked-value", text("******"));
                "wifi"
            }
            "shutdown-dialog" => "menu",
            _ => name,
        };
        if name == "pairing" {
            set(&ui, "pairing-code", text("1234"));
            set(&ui, "pairing-status", text("Simulated board ready to pair"));
            set(&ui, "pairing-action-text", text("Pair"));
        }
        if name == "update" {
            set(&ui, "updates", strings(&["Simulator firmware"]));
            set(&ui, "update-show-dropdown", true);
            set(&ui, "update-body", text("A simulated update is available"));
            set(&ui, "update-primary-text", text("Install"));
        }
        screen(&ui, target);
        if BASELINE.get()
            && !matches!(get(&ui, "screen"), Value::EnumerationValue(_, member) if member == target)
        {
            eprintln!("Base fixture skipped screen {target}: not available in this revision");
            continue;
        }
        ui.show()?;
        i_slint_backend_testing::mock_elapsed_time(Duration::from_millis(600));
        if name == "shutdown-dialog" {
            handle(&ui, &controls, "menu-shutdown", &[]);
        }
        save_snapshot(&ui, &output.join(format!("{name}.png")))?;
        ui.hide()?;
    }
    println!("Captured deterministic screenshots in {}", output.display());
    Ok(())
}

#[cfg(not(target_arch = "wasm32"))]
fn save_snapshot(ui: &ComponentInstance, path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let pixels = ui.window().take_snapshot()?;
    assert!(pixels.width() > 0 && pixels.height() > 0);
    let mut encoder = png::Encoder::new(
        std::fs::File::create(path)?,
        pixels.width(),
        pixels.height(),
    );
    encoder.set_color(png::ColorType::Rgba);
    encoder.set_depth(png::BitDepth::Eight);
    encoder
        .write_header()?
        .write_image_data(pixels.as_bytes())?;
    Ok(())
}
