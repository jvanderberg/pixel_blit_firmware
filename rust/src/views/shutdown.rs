//! Shutdown view - power off confirmation.

use crate::display::Display;
use crate::state::AppState;

pub struct ShutdownView;

impl ShutdownView {
    pub fn render<D: Display>(display: &mut D, _state: &AppState) {
        display.text("Shutdown", 36, 10);
        display.text("Power off?", 32, 30);
        display.text("Sel: confirm", 26, 46);
        display.text("Nxt: cancel", 28, 56);
    }
}
