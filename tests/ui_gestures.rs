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
    import { AppWindow, UiState, Screen } from "../firmware/src/slint/app-window.slint";
    export { UiState, Screen }
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
    export component ProbeTest inherits Window {
        width: 466px; height: 466px;
        out property <int> drag-calls: 0;
        GestureArea {
            width: 100%; height: 100%;
            h-drag(d) => { root.drag-calls += 1; }
            v-drag(d) => { root.drag-calls += 1; }
        }
    }
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
