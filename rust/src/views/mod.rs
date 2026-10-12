//! Views for Pixel Blit firmware.
//!
//! Each screen is an independent view that renders to a Display.
//! RootView dispatches to the appropriate sub-view based on state.

mod boot;
mod main_menu;
mod info;
mod board_address;
mod brightness;
mod rainbow_test;
mod string_length;
mod sd_card;
mod shutdown;

pub use boot::BootView;
pub use main_menu::MainMenuView;
pub use info::InfoView;
pub use board_address::BoardAddressView;
pub use brightness::BrightnessView;
pub use rainbow_test::RainbowTestView;
pub use string_length::StringLengthView;
pub use sd_card::SdCardView;
pub use shutdown::ShutdownView;

use crate::display::Display;
use crate::state::{AppState, Screen};
use reducto::View;
use reducto_view::view;

// Generate ViewBody with view! macro
view! {
    ViewBody<D: Display> for AppState {
        @match state.current_screen() {
            Screen::PoweredOff => {},
            Screen::Boot => <BootView />,
            Screen::MainMenu => <MainMenuView />,
            Screen::Info => <InfoView />,
            Screen::BoardAddress => <BoardAddressView />,
            Screen::SdCard => <SdCardView />,
            Screen::RainbowTest => <RainbowTestView />,
            Screen::StringLength => <StringLengthView />,
            Screen::Brightness => <BrightnessView />,
            Screen::Shutdown => <ShutdownView />,
        }
    }
}

/// Root view that wraps ViewBody with display setup/teardown.
pub struct RootView<D: Display> {
    inner: ViewBody<D>,
}

impl<D: Display> RootView<D> {
    pub fn new(display: D) -> Self {
        Self {
            inner: ViewBody::new(display),
        }
    }
}

impl<D: Display> View for RootView<D> {
    type State = AppState;

    fn render(&mut self, state: &Self::State) {
        self.inner.display.clear_buffer();
        self.inner.render(state);
        self.inner.display.flush().ok();
    }
}
