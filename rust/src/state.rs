//! Application state for Pixel Blit firmware.

/// Run state for tests and playback
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub enum RunState {
    #[default]
    Stopped,
    Running,
}

/// Boot state for startup flow
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub enum BootState {
    /// Still loading (waiting for SD card scan)
    #[default]
    Loading,
    /// Error occurred, waiting for button press
    Error,
    /// Boot complete, show main menu
    Ready,
}

/// Menu entries (7 items)
#[derive(Clone, Copy, Debug, Default, PartialEq)]
#[repr(u8)]
pub enum MenuEntry {
    #[default]
    Info = 0,
    BoardAddress = 1,
    SdCard = 2,
    RainbowTest = 3,
    StringLength = 4,
    Brightness = 5,
    Shutdown = 6,
}

impl MenuEntry {
    pub const COUNT: usize = 7;

    pub fn next(self) -> Self {
        match self {
            Self::Info => Self::BoardAddress,
            Self::BoardAddress => Self::SdCard,
            Self::SdCard => Self::RainbowTest,
            Self::RainbowTest => Self::StringLength,
            Self::StringLength => Self::Brightness,
            Self::Brightness => Self::Shutdown,
            Self::Shutdown => Self::Info,
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::Info => "Info",
            Self::BoardAddress => "Board Address",
            Self::SdCard => "SD Card",
            Self::RainbowTest => "Rainbow Test",
            Self::StringLength => "String Length",
            Self::Brightness => "Brightness",
            Self::Shutdown => "Shutdown",
        }
    }

    pub fn index(self) -> usize {
        self as usize
    }
}

/// Color order for LED strings
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub enum ColorOrder {
    #[default]
    GRB,
    RGB,
    BGR,
    RBG,
    GBR,
    BRG,
}

impl ColorOrder {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::RGB => "RGB",
            Self::GRB => "GRB",
            Self::BGR => "BGR",
            Self::RBG => "RBG",
            Self::GBR => "GBR",
            Self::BRG => "BRG",
        }
    }

    /// Parse color order from string (case insensitive)
    pub fn parse(s: &str) -> Self {
        let s = s.trim();
        if s.len() < 3 {
            return Self::default();
        }
        let bytes = s.as_bytes();
        match (
            bytes[0].to_ascii_uppercase(),
            bytes[1].to_ascii_uppercase(),
            bytes[2].to_ascii_uppercase(),
        ) {
            (b'R', b'G', b'B') => Self::RGB,
            (b'G', b'R', b'B') => Self::GRB,
            (b'B', b'G', b'R') => Self::BGR,
            (b'R', b'B', b'G') => Self::RBG,
            (b'G', b'B', b'R') => Self::GBR,
            (b'B', b'R', b'G') => Self::BRG,
            _ => Self::default(),
        }
    }
}

/// Configuration for a single LED string
#[derive(Clone, Copy, Debug, Default)]
pub struct StringConfig {
    pub pixel_count: u16,
    pub color_order: ColorOrder,
}

/// Info view state
#[derive(Clone, Copy, Default)]
pub struct InfoViewState {
    pub scroll_index: u8,
}

/// Maximum number of .fseq files to display
pub const MAX_FSEQ_FILES: usize = 16;

/// Maximum filename length
pub const MAX_FILENAME_LEN: usize = 32;

/// SD Card view state (filenames stored in static buffer, not here)
#[derive(Clone, Copy, Default)]
pub struct SdCardState {
    pub mounted: bool,
    pub file_count: u8,
    pub scroll_index: u8,
    pub is_playing: bool,
    pub playing_index: u8,
    pub auto_loop: bool,
    pub error_message: Option<&'static str>,
}

/// Rainbow test state
#[derive(Clone, Copy, Default)]
pub struct RainbowTestState {
    pub run_state: RunState,
    pub current_string: u8,
    pub fps: u16,
}

/// String length tool state
#[derive(Clone, Copy)]
pub struct StringLengthState {
    pub run_state: RunState,
    pub current_string: u8,
    pub current_pixel: u16,
    pub lengths: [u16; 32],
}

impl Default for StringLengthState {
    fn default() -> Self {
        Self {
            run_state: RunState::Stopped,
            current_string: 0,
            current_pixel: 1,
            lengths: [0; 32],
        }
    }
}

/// Brightness constants
pub const BRIGHTNESS_MIN: u8 = 1;
pub const BRIGHTNESS_MAX: u8 = 10;
pub const BRIGHTNESS_DEFAULT: u8 = 10;

/// Board ID constants
pub const BOARD_ID_MAX: u8 = 15;

/// String constants
pub const NUM_STRINGS: usize = 32;
pub const MAX_PIXELS_PER_STRING: u16 = 512;

/// Main application state
#[derive(Clone)]
pub struct AppState {
    // Power
    pub is_powered_on: bool,

    // Boot state
    pub boot_state: BootState,
    pub boot_error: Option<&'static str>,

    // Global settings (persisted)
    pub brightness_level: u8,
    pub board_id: u8,

    // Menu navigation
    pub menu_selection: MenuEntry,
    pub in_detail_view: bool,

    // Sub-view states
    pub info_view: InfoViewState,
    pub sd_card: SdCardState,
    pub rainbow_test: RainbowTestState,
    pub string_length: StringLengthState,

    // String configurations (loaded from board config)
    pub strings: [StringConfig; NUM_STRINGS],

    // Timing
    pub uptime_seconds: u32,
}

impl Default for AppState {
    fn default() -> Self {
        Self {
            is_powered_on: true,
            boot_state: BootState::Loading,
            boot_error: None,
            brightness_level: BRIGHTNESS_DEFAULT,
            board_id: 0,
            menu_selection: MenuEntry::default(),
            in_detail_view: false,
            info_view: InfoViewState::default(),
            sd_card: SdCardState::default(),
            rainbow_test: RainbowTestState::default(),
            string_length: StringLengthState::default(),
            strings: [StringConfig::default(); NUM_STRINGS],
            uptime_seconds: 0,
        }
    }
}

impl AppState {
    pub fn new() -> Self {
        Self::default()
    }
}
