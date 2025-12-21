//! Actions for Pixel Blit firmware.
//!
//! Actions represent events that can change application state.
//! They are dispatched through the reducer to produce new state.

/// All possible actions in the application.
#[derive(Clone, Debug)]
pub enum Action {
    // Lifecycle
    /// Initial boot action - triggers first render
    Boot,

    // Button events
    /// Select button pressed
    ButtonSelect,
    /// Next button pressed
    ButtonNext,

    // Timer events
    /// 1-second tick for uptime tracking
    Tick1s,

    // Power events
    /// Toggle power on/off (IR remote or button combo)
    PowerToggle,

    // SD Card events
    /// Request SD card rescan (on boot or menu entry)
    SdRescan,
    /// SD card scanned successfully (filenames in static buffer)
    SdFilesScanned { count: u8 },
    /// Board config loaded from config.csv
    SdConfigLoaded {
        strings: [crate::state::StringConfig; 32],
        string_count: u8,
        max_pixel_count: u16,
    },
    /// SD card error occurred (no card, mount failed, etc.)
    SdCardError { message: &'static str },
    /// config.csv failed to load (file not found, parse error)
    SdConfigError { message: &'static str },

    // Playback events
    /// Skip to next file (IR remote)
    FseqNext,
    /// Toggle auto-loop mode
    AutoToggle,
    /// Current file finished looping
    FseqLoopComplete,
    /// FSEQ playback FPS update
    FseqFpsUpdate { fps: u16 },

    // Brightness events (IR remote)
    /// Increase brightness
    BrightnessUp,
    /// Decrease brightness
    BrightnessDown,

    // Rainbow test events
    /// Frame complete with FPS update
    RainbowFrameComplete { fps: u16 },
}
