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
use crate::state::{AppState, BootState, MenuEntry};
use reducto::View;

/// Root view that owns all sub-views and dispatches based on state.
pub struct RootView<D: Display> {
    display: D,
    boot: BootView,
    main_menu: MainMenuView,
    info: InfoView,
    board_address: BoardAddressView,
    brightness: BrightnessView,
    rainbow_test: RainbowTestView,
    string_length: StringLengthView,
    sd_card: SdCardView,
    shutdown: ShutdownView,
}

impl<D: Display> RootView<D> {
    pub fn new(display: D) -> Self {
        Self {
            display,
            boot: BootView::new(),
            main_menu: MainMenuView::new(),
            info: InfoView::new(),
            board_address: BoardAddressView::new(),
            brightness: BrightnessView::new(),
            rainbow_test: RainbowTestView::new(),
            string_length: StringLengthView::new(),
            sd_card: SdCardView::new(),
            shutdown: ShutdownView::new(),
        }
    }
}

impl<D: Display> View for RootView<D> {
    type State = AppState;

    fn render(&mut self, state: &Self::State) {
        // Clear display
        self.display.clear_buffer();

        // Dispatch to appropriate view
        if !state.is_powered_on {
            // Power off - blank screen (already cleared)
        } else if state.boot_state != BootState::Ready {
            // Boot screen (loading or error)
            self.boot.render_to(&mut self.display, state);
        } else if !state.in_detail_view {
            // Main menu
            self.main_menu.render_to(&mut self.display, state);
        } else {
            // Detail view based on menu selection
            match state.menu_selection {
                MenuEntry::Info => {
                    self.info.render_to(&mut self.display, state);
                }
                MenuEntry::BoardAddress => {
                    self.board_address.render_to(&mut self.display, state);
                }
                MenuEntry::SdCard => {
                    self.sd_card.render_to(&mut self.display, state);
                }
                MenuEntry::RainbowTest => {
                    self.rainbow_test.render_to(&mut self.display, state);
                }
                MenuEntry::StringLength => {
                    self.string_length.render_to(&mut self.display, state);
                }
                MenuEntry::Brightness => {
                    self.brightness.render_to(&mut self.display, state);
                }
                MenuEntry::Shutdown => {
                    self.shutdown.render_to(&mut self.display, state);
                }
            }
        }

        self.display.flush().ok();
    }
}
