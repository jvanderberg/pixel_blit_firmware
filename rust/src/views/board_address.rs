//! Board address view - configure board ID.

use core::fmt::Write;
use heapless::String;

use crate::display::Display;
use crate::state::AppState;

pub struct BoardAddressView;

impl BoardAddressView {
    pub fn render<D: Display>(display: &mut D, state: &AppState) {
        display.text("Board Address", 22, 10);

        let mut line: String<32> = String::new();
        write!(line, "Board ID: {}", state.board_id).ok();
        display.text(&line, 28, 30);

        display.text("Sel: change", 28, 48);
        display.text("Nxt: save & exit", 16, 58);
    }
}
