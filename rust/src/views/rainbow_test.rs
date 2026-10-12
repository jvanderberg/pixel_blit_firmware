//! Rainbow test view - animated rainbow pattern.

use core::fmt::Write;
use heapless::String;

use crate::display::Display;
use crate::state::{AppState, RunState};

pub struct RainbowTestView;

impl RainbowTestView {
    pub fn render<D: Display>(display: &mut D, state: &AppState) {
        display.text("Rainbow Test", 24, 10);

        let mut line: String<32> = String::new();
        write!(
            line,
            "String: {}  FPS: {}",
            state.rainbow_test.current_string, state.rainbow_test.fps
        )
        .ok();
        display.text(&line, 10, 28);

        let status = match state.rainbow_test.run_state {
            RunState::Running => "RUNNING",
            RunState::Stopped => "STOPPED",
        };
        display.text(status, 40, 42);

        display.text("Sel: next str", 22, 54);
        display.text("Nxt: exit", 34, 64);
    }
}
