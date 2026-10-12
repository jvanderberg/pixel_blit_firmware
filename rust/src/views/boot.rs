//! Boot view - loading screen and error display.

use crate::display::Display;
use crate::state::{AppState, BootState};

pub struct BootView;

impl BootView {
    pub fn render<D: Display>(display: &mut D, state: &AppState) {
        match state.boot_state {
            BootState::Loading => {
                display.text("Pixel Blit", 28, 20);
                display.text("Loading...", 32, 40);
            }
            BootState::Error => {
                display.text("Pixel Blit", 28, 10);

                // Show error message
                if let Some(error) = state.boot_error {
                    display.text(error, 4, 30);
                }

                display.text("Using defaults", 20, 42);
                display.text("Press button", 24, 56);
            }
            BootState::Ready => {
                // Should not render boot view when ready
            }
        }
    }
}
