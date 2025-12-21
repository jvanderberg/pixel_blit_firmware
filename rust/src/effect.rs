//! Effects for Pixel Blit firmware.
//!
//! Effects represent side effects that the main loop should perform
//! after a reducer updates state. They are signals only - the main loop
//! reads actual values from state (single source of truth).

use reducto::Effect;

/// All possible effects from reducer actions.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum AppEffect {
    /// State unchanged, skip render
    Unchanged,

    /// State changed, render only (no additional side effects)
    None,

    /// Render + save settings to flash
    Save,

    /// Render + start rainbow test (read string from state)
    RainbowStart,

    /// Render + update rainbow string (read from state)
    RainbowUpdate,

    /// Render + stop rainbow test
    RainbowStop,

    /// Render + start string length test (read position from state)
    StringLengthStart,

    /// Render + update string length position (read from state)
    StringLengthUpdate,

    /// Render + stop string length test
    StringLengthStop,

    /// Render + trigger SD card rescan (read board_id from state)
    SdRescan,

    /// Render + start FSEQ playback (read filename index from state)
    FseqStart,

    /// Render + stop FSEQ playback
    FseqStop,
}

impl Effect for AppEffect {
    fn is_unchanged(&self) -> bool {
        matches!(self, AppEffect::Unchanged)
    }

    fn changed() -> Self {
        AppEffect::None
    }
}
