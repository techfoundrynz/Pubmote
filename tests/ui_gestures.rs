use slint::ComponentHandle;
use slint::platform::software_renderer::{MinimalSoftwareWindow, RepaintBufferType};
use slint::platform::{PlatformError, PointerEventButton, WindowAdapter, WindowEvent};
use std::{cell::RefCell, rc::Rc};
thread_local! { static NEXT: RefCell<Option<Rc<dyn WindowAdapter>>> = RefCell::default(); }
struct Platform;
impl slint::platform::Platform for Platform {
    fn create_window_adapter(&self) -> Result<Rc<dyn WindowAdapter>, PlatformError> {
        Ok(NEXT.with(|n| n.borrow_mut().take().unwrap()))
    }
}
slint::slint! {
    import { GestureArea } from "../firmware/src/slint/ui/gesture-area.slint";
    import { Pager } from "../firmware/src/slint/ui/pager.slint";
    import { ComboBox } from "../firmware/src/slint/ui/combo-box.slint";
    import { Theme } from "../firmware/src/slint/ui/theme.slint";
    import { AppButton } from "../firmware/src/slint/ui/app-button.slint";
    import { AppSlider } from "../firmware/src/slint/ui/app-slider.slint";
    import { AppSwitch } from "../firmware/src/slint/ui/app-switch.slint";
    import { ColorSlider } from "../firmware/src/slint/ui/color-slider.slint";
    import { SettingsScreen } from "../firmware/src/slint/ui/settings.slint";
    import { Navigation } from "../firmware/src/slint/ui/navigation-focus.slint";
    import { Page } from "../firmware/src/slint/ui/page.slint";
    import { Keyboard } from "../firmware/src/slint/ui/keyboard.slint";
    import { AppWindow, UiState, Screen } from "../firmware/src/slint/app-window.slint";
    export { UiState, Screen, Navigation }
    export component PagerTest inherits Window {
        width: 466px; height: 466px;
        in-out property <int> page <=> pager.current-page;
        pager := Pager {
            width: 100%; height: 100%;
            Rectangle { width: 466px; }
            Rectangle { width: 466px; }
        }
    }
    export component AppTest inherits AppWindow { }
    export component InitialFocusTest inherits Window {
        width: 466px; height: 466px;
        in property <bool> empty: false;
        in property <bool> active: true;
        out property <int> clicks: 0;
        out property <int> backs: 0;
        out property <bool> control-focused <=> available.has-focus;
        Page {
            show-back: true;
            back-is-only: root.empty;
            navigation-active: root.active;
            back => { root.backs += 1; }
            VerticalLayout {
                AppButton { text: "Disabled"; enabled: false; clicked => { root.clicks += 100; } }
                available := AppButton { text: "Available"; enabled: !root.empty; last-in-group: true; clicked => { root.clicks += 1; } }
            }
        }
    }
    export component ComboTest inherits Window {
        width: 466px; height: 466px;
        in property <[string]> options;
        in-out property <int> index <=> combo.current-index;
        out property <int> selections: 0;
        init => { Theme.is-square-mode = true; }
        combo := ComboBox {
            x: 50px; y: 30px; width: 300px;
            model: root.options;
            selected(value) => { root.selections += 1; }
        }
    }
    export component KeyboardNavigationTest inherits Window {
        width: 466px; height: 466px;
        in property <bool> secret: false;
        out property <bool> revealed <=> keyboard.revealed;
        out property <int> last-key: -1;
        out property <int> modes: 0;
        out property <int> dones: 0;
        out property <int> cancels: 0;
        keyboard := Keyboard {
            secret: root.secret;
            keys: ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"];
            key(index) => { root.last-key = index; }
            mode => { root.modes += 1; }
            done => { root.dones += 1; }
            cancel => { root.cancels += 1; }
        }
    }
    export component ProbeTest inherits Window {
        width: 466px; height: 466px;
        out property <int> drag-calls: 0;
        GestureArea {
            width: 100%; height: 100%;
            h-drag(d) => { root.drag-calls += 1; }
            v-drag(d) => { root.drag-calls += 1; }
        }
    }
    export component NavigationTest inherits Window {
        width: 466px; height: 466px;
        in-out property <float> value <=> slider.value;
        in-out property <bool> checked <=> toggle.checked;
        in-out property <int> index <=> combo.current-index;
        out property <int> selections: 0;
        out property <int> clicks: 0;
        out property <float> hue <=> color.hue;
        out property <float> sat <=> color.sat;
        out property <float> lit <=> color.lit;
        VerticalLayout {
            AppButton { text: "Disabled"; enabled: false; clicked => { root.clicks += 100; } }
            slider := AppSlider { minimum: 0; maximum: 10; step: 1; }
            toggle := AppSwitch { }
            combo := ComboBox {
                model: ["First", "Second", "Third"];
                selected(value) => { root.selections += 1; }
            }
            color := ColorSlider { }
            AppButton { text: "Last"; clicked => { root.clicks += 1; } }
        }
    }
    export component SettingsNavigationTest inherits Window {
        width: 466px; height: 466px;
        out property <int> page <=> settings.current-page;
        out property <float> brightness <=> settings.brightness;
        out property <int> saved: 0;
        settings := SettingsScreen {
            width: 100%; height: 100%;
            double-press-options: ["None", "Menu"];
            rotation-options: ["0", "90"];
            auto-off-options: ["Off", "On"];
            temp-units-options: ["C", "F"];
            distance-units-options: ["KM", "MI"];
            startup-sound-options: ["Off", "On"];
            save => { root.saved += 1; }
        }
    }
}
fn key(ui: &impl ComponentHandle, value: slint::platform::Key) {
    let text: slint::SharedString = value.into();
    ui.window().dispatch_event(WindowEvent::KeyPressed { text: text.clone() });
    ui.window().dispatch_event(WindowEvent::KeyReleased { text });
}
fn shift_tab(ui: &impl ComponentHandle) {
    ui.window().dispatch_event(WindowEvent::KeyPressed { text: slint::platform::Key::Shift.into() });
    key(ui, slint::platform::Key::Tab);
    ui.window().dispatch_event(WindowEvent::KeyReleased { text: slint::platform::Key::Shift.into() });
}
fn render(w: &MinimalSoftwareWindow) {
    w.draw_if_needed(|renderer| {
        let mut pixels = vec![slint::Rgb8Pixel::default(); 466 * 466];
        renderer.render(&mut pixels, 466);
    });
}
fn connect_focus<T: ComponentHandle + 'static>(ui: &T)
where for<'a> Navigation<'a>: slint::Global<'a, T> {
    let weak = ui.as_weak();
    ui.global::<Navigation>().on_focus_step(move |steps| {
        if let Some(ui) = weak.upgrade() {
            for _ in 0..steps.clamp(-32, 32).abs() {
                key(&ui, if steps < 0 { slint::platform::Key::Backtab } else { slint::platform::Key::Tab });
            }
        }
    });
}
fn initialize_focus<T: ComponentHandle + 'static>(ui: &T, w: &MinimalSoftwareWindow)
where for<'a> Navigation<'a>: slint::Global<'a, T> {
    connect_focus(ui);
    // Let initial screen entry reset navigation before opting into focus.
    settle(w);
    ui.global::<Navigation>().set_active(true);
    settle(w);
}
fn settle(w: &MinimalSoftwareWindow) {
    render(w);
    std::thread::sleep(std::time::Duration::from_millis(280));
    slint::platform::update_timers_and_animations();
    render(w);
    std::thread::sleep(std::time::Duration::from_millis(25));
    slint::platform::update_timers_and_animations();
    render(w);
}

#[test]
fn navigation_focus_waits_for_first_input() {
    use slint::platform::Key::*;
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    let w = window();
    let page = InitialFocusTest::new().unwrap();
    connect_focus(&page);
    page.show().unwrap();
    settle(&w);
    assert!(!page.get_control_focused(), "Idle pages must leave controls unfocused");
    page.global::<Navigation>().set_active(true);
    settle(&w);
    assert!(page.get_control_focused());
    page.hide().unwrap();
    // Exercise button, single-axis and dual-axis input through the same scope
    // used by hardware dispatch and the simulator.
    for first in [Return, Tab, Backtab, UpArrow, DownArrow, LeftArrow, RightArrow] {
        let connects = Rc::new(std::cell::Cell::new(0));
        let w = window();
        let app = AppTest::new().unwrap();
        let state = app.global::<UiState>();
        state.set_screen(Screen::Menu);
        state.set_menu_show_connect(true);
        let count = connects.clone();
        state.on_menu_connect(move || count.set(count.get() + 1));
        // Install traversal callbacks without opting into navigation focus.
        connect_focus(&app);
        app.show().unwrap();
        settle(&w);
        assert!(!app.global::<Navigation>().get_active());
        key(&app, first);
        assert!(app.global::<Navigation>().get_active());
        assert_eq!(connects.get(), 0, "First input must only reveal focus");
        settle(&w);
        key(&app, Return);
        assert_eq!(connects.get(), 1, "Default control is selected after first input");
        app.hide().unwrap();
    }
    let w = window();
    let app = AppTest::new().unwrap();
    let state = app.global::<UiState>();
    state.set_screen(Screen::Menu);
    let rejected = Rc::new(std::cell::Cell::new(0));
    let count = rejected.clone();
    state.on_confirm_dialog_rejected(move || count.set(count.get() + 1));
    connect_focus(&app);
    app.show().unwrap();
    settle(&w);
    state.set_show_confirm_dialog(true);
    settle(&w);
    assert!(state.get_show_confirm_dialog());
    assert!(!app.global::<Navigation>().get_active(), "Dialogs must also wait for input");
    key(&app, Return);
    assert_eq!(rejected.get(), 0);
    settle(&w);
    key(&app, Return);
    assert_eq!(rejected.get(), 1, "First input selects the dialog's safe Cancel action");
}

#[test]
fn navigation_focus_resets_on_screen_changes_and_returning_to_pages() {
    use slint::platform::Key::*;
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    let w = window();
    let app = AppTest::new().unwrap();
    let state = app.global::<UiState>();
    state.set_screen(Screen::Menu);
    state.set_menu_show_connect(true);
    let connects = Rc::new(std::cell::Cell::new(0));
    let count = connects.clone();
    state.on_menu_connect(move || count.set(count.get() + 1));
    app.show().unwrap();
    initialize_focus(&app, &w);
    key(&app, Return);
    assert_eq!(connects.get(), 1);
    for expected in 2..=3 {
        state.set_screen(Screen::Settings);
        settle(&w);
        assert!(!app.global::<Navigation>().get_active());
        let brightness = state.get_brightness();
        key(&app, Return); // reveal brightness focus without entering editing
        settle(&w);
        key(&app, UpArrow); // moves to Back rather than changing brightness
        assert_eq!(state.get_brightness(), brightness);
        assert!(app.global::<Navigation>().get_active());
        // Returning to a previously visited page also requires fresh input.
        state.set_screen(Screen::Menu);
        settle(&w);
        assert!(!app.global::<Navigation>().get_active());
        key(&app, Return);
        assert_eq!(connects.get(), expected - 1);
        settle(&w);
        key(&app, Return);
        assert_eq!(connects.get(), expected);
    }
}

#[test]
fn menu_restores_selected_item_after_returning() {
    use slint::platform::Key::*;
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    let w = window();
    let app = AppTest::new().unwrap();
    let state = app.global::<UiState>();
    state.set_screen(Screen::Menu);
    state.set_menu_show_connect(true);
    let settings = Rc::new(std::cell::Cell::new(0));
    let count = settings.clone();
    let weak = app.as_weak();
    state.on_open_settings(move || {
        count.set(count.get() + 1);
        weak.upgrade().unwrap().global::<UiState>().set_screen(Screen::Settings);
    });
    let weak = app.as_weak();
    state.on_settings_save(move || {
        weak.upgrade().unwrap().global::<UiState>().set_screen(Screen::Menu);
    });
    app.show().unwrap();
    initialize_focus(&app, &w);
    key(&app, Tab); // Pocket mode
    key(&app, Tab); // Settings
    for expected in 1..=2 {
        key(&app, Return);
        assert_eq!(settings.get(), expected);
        settle(&w);
        assert_eq!(state.get_screen(), Screen::Settings);
        key(&app, Escape); // Back/save returns to menu
        settle(&w);
        assert_eq!(state.get_screen(), Screen::Menu);
        assert!(!app.global::<Navigation>().get_active());
        if expected == 1 {
            // Removing a row above Settings must not shift the remembered item.
            state.set_menu_show_connect(false);
        }
        key(&app, Return); // reveal the remembered item, without activation
        assert_eq!(settings.get(), expected);
        settle(&w);
    }
    key(&app, Return);
    assert_eq!(settings.get(), 3, "Returning to the menu keeps Settings selected");
}

#[test]
fn returning_to_stats_resets_menu_selection_and_scroll() {
    use slint::platform::Key::*;
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    let w = window();
    let app = AppTest::new().unwrap();
    let state = app.global::<UiState>();
    state.set_screen(Screen::Menu);
    state.set_menu_show_connect(true);
    let connects = Rc::new(std::cell::Cell::new(0));
    let count = connects.clone();
    state.on_menu_connect(move || count.set(count.get() + 1));
    app.show().unwrap();
    initialize_focus(&app, &w);
    for _ in 0..20 { key(&app, Tab); render(&w); }
    assert!(state.get_scroll_offset_y() > 0, "Lower menu items scroll into view");
    state.set_screen(Screen::Stats);
    settle(&w);
    assert_eq!(state.get_scroll_offset_y(), 0);
    assert!(!app.global::<Navigation>().get_active());
    state.set_screen(Screen::Menu);
    settle(&w);
    assert_eq!(state.get_scroll_offset_y(), 0, "Menu reopens at the top");
    key(&app, Return); // reveal default focus
    assert_eq!(connects.get(), 0);
    settle(&w);
    key(&app, Return);
    assert_eq!(connects.get(), 1, "Menu selection resets to the first available item");
}

#[test]
fn gameplay_input_bypasses_ui_focus_and_preserves_exit() {
    use slint::platform::Key::*;
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    let w = window();
    let app = AppTest::new().unwrap();
    let state = app.global::<UiState>();
    state.set_screen(Screen::Game);
    state.set_game_error("Touch Exit remains available".into());
    let events = Rc::new(RefCell::new(Vec::new()));
    let recorded = events.clone();
    state.on_game_event(move |kind, _, _| recorded.borrow_mut().push(kind));
    let exits = Rc::new(std::cell::Cell::new(0));
    let count = exits.clone();
    state.on_game_back(move || count.set(count.get() + 1));
    connect_focus(&app);
    app.show().unwrap();
    settle(&w);
    key(&app, Return);
    assert_eq!(*events.borrow(), vec![0], "First click must reach gameplay");
    for _ in 0..3 {
        for direction in [UpArrow, DownArrow, LeftArrow, RightArrow] { key(&app, direction); }
        key(&app, Tab);
        key(&app, Backtab);
        key(&app, Return);
        assert!(!app.global::<Navigation>().get_active());
    }
    assert_eq!(*events.borrow(), vec![0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0]);
    assert_eq!(exits.get(), 0, "Gameplay input must not activate touch Exit");
    state.set_processing(true);
    key(&app, Return);
    assert_eq!(events.borrow().len(), 16);
    state.set_processing(false);
    key(&app, Escape);
    assert_eq!(exits.get(), 1, "Double click/Escape still exits gameplay");
    assert!(!app.global::<Navigation>().get_active());
}

#[test]
fn stats_double_press_defaults_to_menu_and_respects_none() {
    use slint::platform::Key::*;
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    let w = window();
    let app = AppTest::new().unwrap();
    let state = app.global::<UiState>();
    state.set_screen(Screen::Stats);
    let menus = Rc::new(std::cell::Cell::new(0));
    let count = menus.clone();
    state.on_stats_swiped_down(move || count.set(count.get() + 1));
    app.show().unwrap();
    initialize_focus(&app, &w);
    assert_eq!(state.get_double_press_index(), 1, "Default is Open menu");
    key(&app, Escape);
    assert_eq!(menus.get(), 1);
    state.set_double_press_index(0);
    key(&app, Escape);
    assert_eq!(menus.get(), 1, "None disables the ride-screen double press");
    state.set_double_press_index(1);
    key(&app, Escape);
    assert_eq!(menus.get(), 2, "Open menu can be re-enabled");
}

#[test]
fn initial_focus_skips_disabled_controls_and_handles_empty_pages() {
    use slint::platform::Key::*;
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    let w = window();
    let ui = InitialFocusTest::new().unwrap();
    ui.show().unwrap();
    initialize_focus(&ui, &w);
    key(&ui, Return);
    assert_eq!(ui.get_clicks(), 1);
    for _ in 0..10 { key(&ui, Tab); }
    key(&ui, Return);
    assert_eq!(ui.get_clicks(), 2, "Bottom focus never wraps");
    key(&ui, Backtab);
    assert_eq!(ui.get_backs(), 0);
    for _ in 0..10 { key(&ui, Backtab); shift_tab(&ui); key(&ui, UpArrow); }
    assert_eq!(ui.get_backs(), 0, "Repeated Up only keeps Back focused");
    key(&ui, Return);
    assert_eq!(ui.get_backs(), 1);
    ui.set_active(false);
    settle(&w);
    ui.set_active(true);
    settle(&w);
    key(&ui, Return);
    assert_eq!(ui.get_clicks(), 3, "Re-entering a retained page resets initial focus");
    ui.hide().unwrap();

    let w = window();
    let empty = InitialFocusTest::new().unwrap();
    empty.set_empty(true);
    empty.show().unwrap();
    initialize_focus(&empty, &w);
    for _ in 0..10 { key(&empty, Tab); }
    key(&empty, Return);
    assert_eq!(empty.get_backs(), 1, "Empty page starts and stays at Back");
    assert_eq!(empty.get_clicks(), 0);
}

#[test]
fn dual_axis_navigation_edits_values_and_changes_pages_without_wrapping() {
    use slint::platform::Key::*;
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    let w = window();
    let ui = NavigationTest::new().unwrap();
    ui.show().unwrap();
    initialize_focus(&ui, &w);
    key(&ui, Tab); // skips disabled button
    key(&ui, Return);
    key(&ui, RightArrow);
    assert_eq!(ui.get_value(), 1.0);
    key(&ui, LeftArrow);
    assert_eq!(ui.get_value(), 0.0);
    for _ in 0..20 { key(&ui, LeftArrow); }
    assert_eq!(ui.get_value(), 0.0);
    key(&ui, UpArrow);
    key(&ui, DownArrow);
    assert_eq!(ui.get_value(), 0.0);
    key(&ui, Return); // leave editing
    key(&ui, RightArrow); // switch
    key(&ui, Return);
    assert!(ui.get_checked());
    key(&ui, DownArrow); // dropdown
    key(&ui, Return);
    key(&ui, RightArrow);
    assert_eq!(ui.get_index(), 1);
    key(&ui, LeftArrow);
    assert_eq!(ui.get_index(), 0);
    key(&ui, Return);
    key(&ui, RightArrow); // color
    key(&ui, Return);
    key(&ui, RightArrow);
    assert_eq!(ui.get_hue(), 5.0);
    key(&ui, LeftArrow);
    assert_eq!(ui.get_hue(), 0.0);
    key(&ui, Escape);
    ui.hide().unwrap();

    let w = window();
    let settings = SettingsNavigationTest::new().unwrap();
    settings.show().unwrap();
    initialize_focus(&settings, &w);
    key(&settings, DownArrow); // page dots
    for expected in 1..8 {
        key(&settings, RightArrow); // changes page without entering editing
        settle(&w);
        assert_eq!(settings.get_page(), expected);
    }
    key(&settings, RightArrow);
    assert_eq!(settings.get_page(), 7);
    for expected in (0..7).rev() {
        key(&settings, LeftArrow);
        settle(&w);
        assert_eq!(settings.get_page(), expected);
    }
    key(&settings, LeftArrow);
    assert_eq!(settings.get_page(), 0);
    key(&settings, UpArrow); // brightness
    key(&settings, UpArrow); // Back
    key(&settings, LeftArrow); // must not activate Back
    assert_eq!(settings.get_saved(), 0);
    key(&settings, UpArrow);
    assert_eq!(settings.get_saved(), 0, "Up at Back cannot activate it");
    key(&settings, Return);
    assert_eq!(settings.get_saved(), 1);
}

#[test]
fn keyboard_arrows_follow_rows_columns_and_footer_without_wrapping() {
    use slint::platform::Key::*;
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    for secret in [false, true] {
        let w = window();
        let ui = KeyboardNavigationTest::new().unwrap();
        ui.set_secret(secret);
        ui.show().unwrap();
        initialize_focus(&ui, &w);
        key(&ui, Return);
        assert_eq!(ui.get_last_key(), 0);
        for _ in 0..4 { key(&ui, LeftArrow); }
        key(&ui, Return);
        assert_eq!(ui.get_last_key(), 0, "Left stops at the column edge");
        key(&ui, RightArrow);
        key(&ui, DownArrow);
        key(&ui, Return);
        assert_eq!(ui.get_last_key(), 4, "Down keeps the column");
        key(&ui, RightArrow);
        for _ in 0..4 { key(&ui, RightArrow); }
        key(&ui, Return);
        assert_eq!(ui.get_last_key(), 5, "Right cannot wrap to another row");
        key(&ui, DownArrow);
        key(&ui, DownArrow);
        key(&ui, Return);
        assert_eq!(ui.get_last_key(), 11);
        key(&ui, DownArrow); // Done
        for _ in 0..4 { key(&ui, DownArrow); key(&ui, RightArrow); }
        key(&ui, Return);
        assert_eq!(ui.get_dones(), 1, "Footer edges cannot wrap");
        key(&ui, UpArrow);
        key(&ui, Return);
        assert_eq!(ui.get_last_key(), 11, "Up from Done returns to the rightmost key");
        key(&ui, DownArrow);
        key(&ui, LeftArrow);
        if secret {
            key(&ui, Return); // Reveal
            assert!(ui.get_revealed());
            key(&ui, UpArrow);
            key(&ui, Return);
            assert_eq!(ui.get_last_key(), 10);
            key(&ui, DownArrow);
            key(&ui, LeftArrow);
        }
        key(&ui, Return); // Mode
        assert_eq!(ui.get_modes(), 1);
        key(&ui, UpArrow);
        key(&ui, Return);
        assert_eq!(ui.get_last_key(), 9);
        for _ in 0..3 { key(&ui, UpArrow); }
        key(&ui, UpArrow); // Back highlight
        assert_eq!(ui.get_cancels(), 0);
        for _ in 0..10 { key(&ui, UpArrow); key(&ui, LeftArrow); }
        assert_eq!(ui.get_cancels(), 0, "Keyboard directions cannot activate Back");
        key(&ui, Return); // Back activate
        assert_eq!(ui.get_cancels(), 1);
        ui.hide().unwrap();
    }
}

#[test]
fn single_axis_edits_controls_then_returns_to_focus_navigation() {
    use slint::platform::Key::*;
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    let w = window();
    let ui = NavigationTest::new().unwrap();
    ui.show().unwrap();
    initialize_focus(&ui, &w);
    key(&ui, Tab); // skips disabled button, focuses slider
    key(&ui, Return);
    key(&ui, Backtab); // up increases
    assert_eq!(ui.get_value(), 1.0);
    shift_tab(&ui);
    assert_eq!(ui.get_value(), 2.0, "Desktop Shift+Tab also increases the edited value");
    key(&ui, Tab);
    key(&ui, Tab); // down decreases, stays in editing
    assert_eq!(ui.get_value(), 0.0);
    for _ in 0..20 { key(&ui, Backtab); }
    assert_eq!(ui.get_value(), 10.0);
    key(&ui, Escape); // exit editing
    key(&ui, Tab);
    key(&ui, Return);
    assert!(ui.get_checked());
    key(&ui, Tab);
    key(&ui, Return);
    key(&ui, Backtab);
    assert_eq!(ui.get_index(), 1);
    assert_eq!(ui.get_selections(), 1);
    key(&ui, Return); // finish editing
    key(&ui, Tab);
    key(&ui, Return); // hue
    key(&ui, Backtab);
    assert_eq!(ui.get_hue(), 5.0);
    key(&ui, Return); // saturation
    key(&ui, Tab);
    assert_eq!(ui.get_sat(), 98.0);
    key(&ui, Return); // lightness
    key(&ui, Backtab);
    assert_eq!(ui.get_lit(), 52.0);
    key(&ui, Return); // finish
    key(&ui, Tab);
    key(&ui, Return);
    assert_eq!(ui.get_clicks(), 1);
}

#[test]
fn page_selector_reaches_every_settings_page_and_back() {
    use slint::platform::Key::*;
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    let w = window();
    let ui = SettingsNavigationTest::new().unwrap();
    ui.show().unwrap();
    initialize_focus(&ui, &w);
    key(&ui, Backtab);
    assert_eq!(ui.get_saved(), 0, "First Up only highlights Back");
    shift_tab(&ui);
    assert_eq!(ui.get_saved(), 0, "Further Up stops at Back/save");
    key(&ui, Return);
    assert_eq!(ui.get_saved(), 1, "Click activates Back/save");
    key(&ui, Tab); // brightness again
    key(&ui, Return);
    key(&ui, Backtab);
    assert!(ui.get_brightness() > 128.0);
    key(&ui, Return);
    key(&ui, Tab); // footer page selector
    key(&ui, Return);
    for expected in 1..8 {
        key(&ui, Tab);
        // Finish the page animation so visibility is representative.
        std::thread::sleep(std::time::Duration::from_millis(260));
        slint::platform::update_timers_and_animations();
        assert_eq!(ui.get_page(), expected);
    }
    key(&ui, Tab);
    assert_eq!(ui.get_page(), 7);
    key(&ui, Return); // leave selector editing
    for _ in 0..10 { key(&ui, Tab); }
    assert_eq!(ui.get_page(), 7, "Down stops at the footer page selector");
    key(&ui, Backtab); // theme color
    key(&ui, Backtab); // header save/back
    assert_eq!(ui.get_saved(), 1);
    key(&ui, Backtab);
    assert_eq!(ui.get_saved(), 1);
    key(&ui, Return);
    assert_eq!(ui.get_saved(), 2);
}

#[test]
fn modal_traps_focus_and_processing_blocks_activation() {
    use slint::platform::Key::*;
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    let w = window();
    let app = AppTest::new().unwrap();
    let state = app.global::<UiState>();
    state.set_screen(Screen::Menu);
    let accepted = Rc::new(std::cell::Cell::new(0));
    let rejected = Rc::new(std::cell::Cell::new(0));
    let menu = Rc::new(std::cell::Cell::new(0));
    let count = accepted.clone();
    state.on_confirm_dialog_accepted(move || count.set(count.get() + 1));
    let count = rejected.clone();
    state.on_confirm_dialog_rejected(move || count.set(count.get() + 1));
    let count = menu.clone();
    state.on_menu_back(move || count.set(count.get() + 1));
    app.show().unwrap();
    initialize_focus(&app, &w);
    state.set_show_confirm_dialog(true);
    render(&w);
    slint::platform::update_timers_and_animations();
    std::thread::sleep(std::time::Duration::from_millis(25));
    slint::platform::update_timers_and_animations();
    key(&app, Return); // initial focus is the safe Cancel action
    assert_eq!(rejected.get(), 1);
    for _ in 0..10 { key(&app, LeftArrow); }
    key(&app, Return);
    assert_eq!(rejected.get(), 2, "Up must stop at Cancel");
    assert_eq!(accepted.get(), 0);
    for _ in 0..10 { key(&app, RightArrow); }
    key(&app, Return);
    assert_eq!(accepted.get(), 1, "Down must stop at Confirm");
    for _ in 0..10 { key(&app, LeftArrow); }
    key(&app, Return);
    assert_eq!(rejected.get(), 3, "Up from Confirm must stop at Cancel");
    key(&app, Tab); // Confirm remains reachable after reversing direction
    assert_eq!(menu.get(), 0);
    state.set_processing(true);
    key(&app, Return);
    assert_eq!(accepted.get(), 1);
    state.set_processing(false);
    state.set_show_confirm_dialog(false);
    // Re-enter the page to verify that closing a modal cannot interfere with
    // fresh initial focus, and processing defers the initialization.
    state.set_screen(Screen::Stats);
    settle(&w);
    state.set_processing(true);
    state.set_screen(Screen::Menu);
    settle(&w);
    key(&app, Return);
    assert_eq!(menu.get(), 0);
    state.set_processing(false);
    settle(&w);
    key(&app, Return); // first input reveals focus after screen re-entry
    settle(&w);
    key(&app, Backtab);
    assert_eq!(menu.get(), 0);
    key(&app, Backtab);
    assert_eq!(menu.get(), 0, "Up must not activate Back");
    key(&app, Return);
    assert_eq!(menu.get(), 1);

    render(&w);
    slint::platform::update_timers_and_animations();
    state.set_confirm_dialog_show_cancel(false);
    state.set_show_confirm_dialog(true);
    render(&w);
    slint::platform::update_timers_and_animations();
    std::thread::sleep(std::time::Duration::from_millis(25));
    slint::platform::update_timers_and_animations();
    for _ in 0..10 { key(&app, Tab); key(&app, Backtab); }
    key(&app, Return);
    assert_eq!(accepted.get(), 2, "Single-action dialogs must retain their action");
    assert_eq!(rejected.get(), 3);
    assert_eq!(menu.get(), 1, "Dialog navigation must not reach the background");
}

#[test]
fn single_axis_reaches_scrolled_menu_actions_and_every_keyboard_key() {
    use slint::platform::Key::*;
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    let w = window();
    let app = AppTest::new().unwrap();
    let state = app.global::<UiState>();
    state.set_screen(Screen::Menu);
    state.set_menu_show_connect(true);
    state.set_dimming_mode_supported(true);
    state.set_led_mode_supported(true);
    state.set_joystick_supported(true);
    state.set_imu_supported(true);
    let actions = Rc::new(RefCell::new(std::collections::BTreeSet::new()));
    macro_rules! track {
        ($callback:ident, $label:literal) => {
            let actions = actions.clone();
            state.$callback(move || { actions.borrow_mut().insert($label); });
        };
    }
    track!(on_menu_connect, "connect");
    track!(on_menu_pocket_mode, "pocket");
    track!(on_menu_toggle_dimming, "dimming");
    track!(on_menu_toggle_led, "led");
    track!(on_open_settings, "settings");
    track!(on_open_input_calibration, "inputs");
    track!(on_open_imu_calibration, "imu");
    track!(on_open_pairing, "pairing");
    track!(on_open_wifi, "wifi");
    track!(on_open_about, "about");
    track!(on_menu_shutdown, "shutdown");
    track!(on_menu_back, "back");
    let connects = Rc::new(std::cell::Cell::new(0));
    let backs = Rc::new(std::cell::Cell::new(0));
    let count = backs.clone();
    let recorded = actions.clone();
    state.on_menu_back(move || {
        count.set(count.get() + 1);
        recorded.borrow_mut().insert("back");
    });
    let count = connects.clone();
    let recorded = actions.clone();
    state.on_menu_connect(move || {
        count.set(count.get() + 1);
        recorded.borrow_mut().insert("connect");
    });
    app.show().unwrap();
    initialize_focus(&app, &w);
    key(&app, Return);
    assert_eq!(connects.get(), 1, "Initial focus skips Back and selects Connect");
    shift_tab(&app);
    assert_eq!(backs.get(), 0, "First Up highlights Back");
    shift_tab(&app);
    for _ in 0..10 { key(&app, UpArrow); shift_tab(&app); }
    assert_eq!(backs.get(), 0, "Holding Up stops at Back");
    key(&app, Return);
    assert_eq!(backs.get(), 1, "Click activates Back");
    for _ in 0..24 { key(&app, Tab); render(&w); key(&app, Return); }
    assert_eq!(actions.borrow().len(), 12, "Every menu action, including rows below the fold: {:?}", actions.borrow());
    assert_eq!(connects.get(), 2, "Down must not wrap back to Connect");
    assert_eq!(backs.get(), 1, "Down must not jump to Back after the last menu item");
    for _ in 0..11 { key(&app, Backtab); render(&w); }
    assert_eq!(backs.get(), 1, "Moving to Back must not activate it");
    key(&app, Backtab);
    assert_eq!(backs.get(), 1, "Up while Back is focused must not activate it");
    key(&app, Return);
    assert_eq!(backs.get(), 2, "Back needs a button press");
    assert_eq!(connects.get(), 2);

    let keys = Rc::new(RefCell::new(std::collections::BTreeSet::new()));
    let first_keys = Rc::new(std::cell::Cell::new(0));
    let cancels = Rc::new(std::cell::Cell::new(0));
    let count = cancels.clone();
    state.on_wifi_cancel(move || count.set(count.get() + 1));
    let first_count = first_keys.clone();
    let recorded = keys.clone();
    state.on_wifi_key(move |index| {
        recorded.borrow_mut().insert(index);
        if index == 0 { first_count.set(first_count.get() + 1); }
    });
    state.set_wifi_keys(slint::ModelRc::new(slint::VecModel::from(
        (0..12).map(|i| format!("Key {i}").into()).collect::<Vec<_>>(),
    )));
    state.set_wifi_editing(true);
    state.set_screen(Screen::Wifi);
    settle(&w);
    key(&app, Return); // first input enables navigation on the new screen
    settle(&w);
    key(&app, Return); // initial keyboard focus is its first key
    for _ in 0..32 { key(&app, Tab); render(&w); key(&app, Return); }
    assert_eq!(keys.borrow().iter().copied().collect::<Vec<_>>(), (0..12).collect::<Vec<_>>());
    assert_eq!(first_keys.get(), 1, "Keyboard focus must not wrap at the bottom");
    for _ in 0..14 { shift_tab(&app); }
    assert_eq!(first_keys.get(), 1, "Up must not wrap to the keyboard keys");
    assert_eq!(cancels.get(), 0, "Arriving at Back only highlights it");
    shift_tab(&app);
    assert_eq!(cancels.get(), 0, "Up from keyboard Back stays there");
    key(&app, Return);
    assert_eq!(cancels.get(), 1, "Click on keyboard Back cancels");
}
fn window() -> Rc<MinimalSoftwareWindow> {
    let w = MinimalSoftwareWindow::new(RepaintBufferType::NewBuffer);
    NEXT.with(|n| *n.borrow_mut() = Some(w.clone()));
    w.set_size(slint::PhysicalSize::new(466, 466));
    w
}
fn swipe(ui: &impl ComponentHandle, points: &[(f32, f32)]) {
    let pos = |p: (f32, f32)| slint::LogicalPosition::new(p.0, p.1);
    ui.window().dispatch_event(WindowEvent::PointerPressed {
        position: pos(points[0]),
        button: PointerEventButton::Left,
    });
    for &p in &points[1..points.len() - 1] {
        ui.window()
            .dispatch_event(WindowEvent::PointerMoved { position: pos(p) });
    }
    ui.window().dispatch_event(WindowEvent::PointerReleased {
        position: pos(*points.last().unwrap()),
        button: PointerEventButton::Left,
    });
}
#[test]
fn reversing_swipes_cancels_page_and_menu_navigation() {
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    let _w = window();
    let pager = PagerTest::new().unwrap();
    pager.show().unwrap();
    let horizontal_cases: &[(i32, &[f32], i32)] = &[
        (0, &[420., 400., 80., 200., 200.], 0),
        (1, &[40., 60., 380., 250., 250.], 1),
        (0, &[420., 400., 100., 100.], 1),
        (1, &[40., 60., 380., 380.], 0),
        (0, &[420., 400., 80., 85., 85.], 1),
        (0, &[420., 400., 80., 200., 160., 160.], 1),
        (0, &[420., 400., 80., 200.], 0),
        (0, &[420., 400., 100.], 1),
        (0, &[420., 400., 80., 415., 415.], 0),
        (0, &[40., 60., 380., 380.], 0),
        (1, &[420., 400., 100., 100.], 1),
    ];
    for &(start, positions, expected) in horizontal_cases {
        pager.set_page(start);
        let points: Vec<_> = positions.iter().map(|&x| (x, 200.)).collect();
        swipe(&pager, &points);
        assert_eq!(
            pager.get_page(),
            expected,
            "horizontal {start}: {positions:?}"
        );
    }
    pager.hide().unwrap();
    let vertical_cases: &[(&[f32], i32)] = &[
        (&[60., 80., 410., 250., 250.], 0),
        (&[60., 80., 410., 410.], 1),
        (&[60., 80., 410., 405., 405.], 1),
        (&[60., 80., 410., 250.], 0),
        (&[60., 80., 410.], 1),
        (&[60., 80., 410., 90., 90.], 0),
        (&[60., 80., 410., 250., 300., 300.], 1),
    ];
    for &(positions, expected) in vertical_cases {
        let _w = window();
        let app = AppTest::new().unwrap();
        let state = app.global::<UiState>();
        state.set_screen(Screen::Stats);
        let commits = Rc::new(std::cell::Cell::new(0));
        let count = commits.clone();
        state.on_stats_swiped_down(move || count.set(count.get() + 1));
        app.show().unwrap();
        let points: Vec<_> = positions.iter().map(|&y| (80., y)).collect();
        swipe(&app, &points);
        assert_eq!(commits.get(), expected, "vertical: {positions:?}");
        app.hide().unwrap();
    }
    let _w = window();
    let probe = ProbeTest::new().unwrap();
    probe.show().unwrap();
    probe.window().dispatch_event(WindowEvent::PointerMoved {
        position: slint::LogicalPosition::new(200., 200.),
    });
    assert_eq!(probe.get_drag_calls(), 0, "hover must not begin a drag");
    swipe(
        &probe,
        &[(420., 200.), (400., 200.), (300., 200.), (300., 200.)],
    );
    let calls = probe.get_drag_calls();
    probe.window().dispatch_event(WindowEvent::PointerMoved {
        position: slint::LogicalPosition::new(100., 200.),
    });
    assert_eq!(
        probe.get_drag_calls(),
        calls,
        "moves after release must not continue the drag"
    );
    probe.hide().unwrap();
}

#[test]
fn dropdown_drag_does_not_select_and_next_tap_still_works() {
    slint::platform::set_platform(Box::new(Platform)).unwrap();
    for option_count in [3, 12] {
        for points in [
            vec![(150., 140.), (150., 160.), (150., 160.)],
            vec![(150., 140.), (150., 165.), (150., 140.), (150., 140.)],
            vec![(150., 220.), (150., 180.), (150., 140.), (150., 140.)],
            // The release position alone must also reject a drag.
            vec![(150., 140.), (150., 165.)],
        ] {
            let _w = window();
            let ui = ComboTest::new().unwrap();
            ui.set_options(slint::ModelRc::new(slint::VecModel::from(
                (0..option_count)
                    .map(|i| format!("Option {i}").into())
                    .collect::<Vec<_>>(),
            )));
            ui.show().unwrap();
            swipe(&ui, &[(150., 60.), (150., 60.)]);
            swipe(&ui, &points);
            assert_eq!(ui.get_selections(), 0, "{option_count} options: {points:?}");
            assert_eq!(ui.get_index(), 0);
            // Tap inside a visible row, away from the gaps exposed by scrolling.
            swipe(&ui, &[(150., 130.), (152., 132.), (152., 132.)]);
            assert_eq!(
                ui.get_selections(),
                1,
                "{option_count} options, tap after drag: {points:?}"
            );
            ui.hide().unwrap();
        }
    }
}
