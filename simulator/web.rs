//! Browser entry point. UI, callbacks, service doubles and telemetry are shared
//! with the desktop simulator; only loading and window setup differ.
#![cfg(target_arch = "wasm32")]

#[allow(dead_code, unused_imports)]
#[path = "main.rs"]
mod simulation;

use slint::{Timer, TimerMode};
use slint_interpreter::{Compiler, ComponentHandle, ComponentInstance, Value};
use std::{cell::RefCell, path::Path, time::Duration};
use wasm_bindgen::{JsCast, prelude::*};
use wasm_bindgen_futures::JsFuture;
use web_time::Instant;

thread_local! { static NEXT_CANVAS: RefCell<Option<String>> = const { RefCell::new(None) }; }
fn error(value: impl std::fmt::Display) -> JsValue {
    JsValue::from_str(&value.to_string())
}

async fn fetch(url: &str) -> Result<String, JsValue> {
    let response = JsFuture::from(
        web_sys::window()
            .ok_or_else(|| error("No browser window"))?
            .fetch_with_str(url),
    )
    .await?;
    let response: web_sys::Response = response.dyn_into()?;
    if !response.ok() {
        return Err(error(format!("{}: HTTP {}", url, response.status())));
    }
    JsFuture::from(response.text()?)
        .await?
        .as_string()
        .ok_or_else(|| error("Expected UI source"))
}

fn browser_source(source: String) -> String {
    // Fonts are bundled below; the browser has no filesystem for Slint's TTF imports.
    let source = source
        .lines()
        .filter(|line| !(line.trim_start().starts_with("import \"") && line.contains(".ttf\"")))
        .collect::<Vec<_>>()
        .join("\n");
    // The MCU arc fast path reads arc-center-* directly. FemtoVG uses
    // MoveTo/ArcTo, which lack the MCU's half-stroke translation.
    // Compensate only for our arc components; keep device sources intact.
    if source.contains("arc-center-x:") {
        source.replace("fit: preserve;", "fit: preserve; x: root.thickness / 2; y: root.thickness / 2;")
    } else {
        source
    }
}

async fn load(url: &str, name: &str, canvas: &str) -> Result<ComponentInstance, JsValue> {
    let mut compiler = Compiler::default();
    compiler.set_style("fluent".into());
    compiler.set_file_loader(|path: &Path| {
        let url = path.to_string_lossy().to_string();
        Box::pin(async move {
            if !url.starts_with("http://") && !url.starts_with("https://") {
                return None;
            }
            Some(
                fetch(&url)
                    .await
                    .map(browser_source)
                    .map_err(|e| std::io::Error::other(format!("{e:?}"))),
            )
        })
    });
    let source = browser_source(fetch(url).await?);
    let result = compiler.build_from_source(source, url.into()).await;
    if result.has_errors() {
        return Err(error(
            result
                .diagnostics()
                .map(|d| d.to_string())
                .collect::<Vec<_>>()
                .join("\n"),
        ));
    }
    NEXT_CANVAS.with(|next| *next.borrow_mut() = Some(canvas.into()));
    result
        .component(name)
        .ok_or_else(|| error("UI component missing"))?
        .create()
        .map_err(error)
}

#[wasm_bindgen]
pub struct WebSimulator {
    ui: ComponentInstance,
    controls: ComponentInstance,
    _timer: Timer,
}

#[wasm_bindgen]
impl WebSimulator {
    /// Test hooks use the same properties and callbacks as the real controls.
    pub fn state(&self, name: &str) -> JsValue {
        match simulation::get(&self.ui, name) {
            Value::String(s) => JsValue::from_str(&s),
            Value::Number(n) => JsValue::from_f64(n),
            Value::Bool(b) => JsValue::from_bool(b),
            Value::EnumerationValue(_, member) => JsValue::from_str(&member),
            _ => JsValue::NULL,
        }
    }
    pub fn control(&self, name: &str, value: JsValue) -> Result<(), JsValue> {
        let value = if let Some(b) = value.as_bool() {
            Value::Bool(b)
        } else if let Some(n) = value.as_f64() {
            Value::Number(n)
        } else {
            return Err(error("Control value must be a number or boolean"));
        };
        self.controls
            .set_property(name, value)
            .map_err(|e| error(format!("{e:?}")))?;
        simulation::tick(&self.ui, &self.controls, 0.0);
        Ok(())
    }
    pub fn navigate(&self, screen: &str) {
        simulation::screen(&self.ui, screen);
    }
    pub fn invoke(&self, callback: &str) -> Result<(), JsValue> {
        self.ui
            .invoke_global("UiState", callback, &[])
            .map_err(|e| error(format!("{e:?}")))?;
        Ok(())
    }
}

#[wasm_bindgen]
pub async fn start(
    base_url: String,
    width: u32,
    height: u32,
    square: bool,
) -> Result<WebSimulator, JsValue> {
    console_error_panic_hook::set_once();
    if !(120..=1200).contains(&width) || !(120..=1200).contains(&height) {
        return Err(error("Invalid panel size"));
    }
    let backend = i_slint_backend_winit::Backend::builder()
        .with_spawn_event_loop(true)
        .with_renderer_name("femtovg")
        .with_window_attributes_hook(|attrs| {
            use i_slint_backend_winit::winit::platform::web::WindowAttributesExtWebSys;
            NEXT_CANVAS.with(|next| {
                if let Some(id) = next.borrow_mut().take() {
                    let canvas = web_sys::window()
                        .unwrap()
                        .document()
                        .unwrap()
                        .get_element_by_id(&id)
                        .unwrap()
                        .dyn_into::<web_sys::HtmlCanvasElement>()
                        .unwrap();
                    attrs.with_canvas(Some(canvas)).with_active(false)
                } else {
                    attrs
                }
            })
        })
        .build()
        .map_err(error)?;
    slint::platform::set_platform(Box::new(backend)).map_err(error)?;
    // Register font bytes before any text is shaped, including control widgets.
    for bytes in [
        include_bytes!("../firmware/assets/Saira-SemiBold.ttf").as_slice(),
        include_bytes!("../firmware/assets/JetBrainsMono-Medium.ttf").as_slice(),
        include_bytes!("../firmware/assets/lucide-icons.ttf").as_slice(),
    ] {
        let blob = slint::fontique_011::fontique::Blob::new(std::sync::Arc::new(bytes.to_vec()));
        slint::fontique_011::shared_collection().register_fonts(blob, None);
    }
    let ui = load(
        &format!("{base_url}firmware/src/slint/app-window.slint"),
        "AppWindow",
        "remote",
    )
    .await?;
    let controls = load(
        &format!("{base_url}controls.slint"),
        "SimulatorControls",
        "controls",
    )
    .await?;
    simulation::initialize(&ui, &controls, width as f64, height as f64, square);
    simulation::tick(&ui, &controls, 0.0);
    let timer = Timer::default();
    let weak = ui.as_weak();
    let cweak = controls.as_weak();
    let start = Instant::now();
    timer.start(TimerMode::Repeated, Duration::from_millis(50), move || {
        if let (Some(ui), Some(controls)) = (weak.upgrade(), cweak.upgrade()) {
            simulation::tick(&ui, &controls, start.elapsed().as_secs_f64());
        }
    });
    ui.show().map_err(error)?;
    controls.show().map_err(error)?;
    slint_interpreter::run_event_loop().map_err(error)?;
    Ok(WebSimulator {
        ui,
        controls,
        _timer: timer,
    })
}
