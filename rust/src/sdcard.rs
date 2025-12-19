//! SD Card driver for reading config.csv and .fseq files.
//!
//! Uses embedded-sdmmc with SPI0 on GPIO 36-39.

use heapless::String;

// Re-export types from state
pub use crate::state::{ColorOrder, StringConfig, MAX_FSEQ_FILES, MAX_FILENAME_LEN};

/// Maximum number of strings per board
pub const MAX_STRINGS: usize = 32;

/// Board configuration loaded from config.csv
#[derive(Clone, Debug)]
pub struct BoardConfig {
    pub strings: [StringConfig; MAX_STRINGS],
    pub string_count: u8,
    pub max_pixel_count: u16,
}

impl Default for BoardConfig {
    fn default() -> Self {
        Self {
            strings: [StringConfig {
                pixel_count: 50,
                color_order: ColorOrder::GRB,
            }; MAX_STRINGS],
            string_count: MAX_STRINGS as u8,
            max_pixel_count: 50,
        }
    }
}

/// List of .fseq filenames found on SD card
#[derive(Clone, Debug)]
pub struct FileList {
    pub names: [String<MAX_FILENAME_LEN>; MAX_FSEQ_FILES],
    pub count: u8,
}

impl Default for FileList {
    fn default() -> Self {
        Self {
            names: Default::default(),
            count: 0,
        }
    }
}

// ============================================================================
// Static file list (shared between SD task and views)
// ============================================================================

#[cfg(feature = "firmware")]
mod file_list_static {
    use super::*;
    use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
    use embassy_sync::blocking_mutex::Mutex;
    use core::cell::RefCell;

    static FILE_LIST: Mutex<CriticalSectionRawMutex, RefCell<FileList>> =
        Mutex::new(RefCell::new(FileList {
            names: [const { String::new() }; MAX_FSEQ_FILES],
            count: 0
        }));

    /// Store a new file list
    pub fn set_file_list(files: FileList) {
        FILE_LIST.lock(|list| {
            *list.borrow_mut() = files;
        });
    }

    /// Get a filename by index
    pub fn get_filename(index: usize) -> String<MAX_FILENAME_LEN> {
        FILE_LIST.lock(|list| {
            let list = list.borrow();
            if index < list.count as usize {
                list.names[index].clone()
            } else {
                String::new()
            }
        })
    }

    /// Get the file count
    pub fn get_file_count() -> u8 {
        FILE_LIST.lock(|list| list.borrow().count)
    }
}

#[cfg(feature = "firmware")]
pub use file_list_static::{set_file_list, get_filename, get_file_count};

/// SD card error types
#[derive(Clone, Debug)]
pub enum SdError {
    /// Card not present or not responding
    NoCard,
    /// Failed to mount filesystem
    MountFailed,
    /// File not found
    FileNotFound,
    /// Read error
    ReadError,
    /// Parse error in config file
    ParseError,
}

impl SdError {
    /// Get a short description for display
    pub fn message(&self) -> &'static str {
        match self {
            Self::NoCard => "No SD card",
            Self::MountFailed => "Mount failed",
            Self::FileNotFound => "File not found",
            Self::ReadError => "Read error",
            Self::ParseError => "Parse error",
        }
    }
}

// ============================================================================
// Config parsing (pure, testable)
// ============================================================================

/// Parse a single line from config.csv
/// Format: "pixel_count,COLOR_ORDER" or "0" for disabled
pub fn parse_config_line(line: &str) -> Option<StringConfig> {
    let line = line.trim();

    // Skip empty lines and comments
    if line.is_empty() || line.starts_with('#') {
        return None;
    }

    // Check for "0" (disabled string)
    if line == "0" || line.starts_with("0,") || line.starts_with("0 ") {
        return Some(StringConfig {
            pixel_count: 0,
            color_order: ColorOrder::GRB,
        });
    }

    // Find comma
    let comma_pos = line.find(',')?;

    // Parse pixel count
    let pixel_str = line[..comma_pos].trim();
    let pixel_count: u16 = pixel_str.parse().ok()?;

    // Parse color order
    let color_str = &line[comma_pos + 1..];
    let color_order = ColorOrder::parse(color_str);

    Some(StringConfig {
        pixel_count,
        color_order,
    })
}

/// Parse config.csv buffer for a specific board
pub fn parse_config_buffer(buffer: &str, board_id: u8) -> Result<BoardConfig, SdError> {
    let mut config = BoardConfig::default();
    config.string_count = 0;
    config.max_pixel_count = 0;

    // Calculate which rows we need
    let start_row = (board_id as usize) * MAX_STRINGS;
    let end_row = start_row + MAX_STRINGS;

    let mut current_row = 0;

    for line in buffer.lines() {
        let line = line.trim();

        // Skip comments and empty lines (don't count as rows)
        if line.is_empty() || line.starts_with('#') {
            continue;
        }

        // Process if in our board's range
        if current_row >= start_row && current_row < end_row {
            let string_index = current_row - start_row;

            if let Some(string_config) = parse_config_line(line) {
                config.strings[string_index] = string_config;

                if string_config.pixel_count > config.max_pixel_count {
                    config.max_pixel_count = string_config.pixel_count;
                }
                if string_config.pixel_count > 0 {
                    config.string_count = (string_index + 1) as u8;
                }
            } else {
                return Err(SdError::ParseError);
            }
        }

        current_row += 1;

        // Stop if we've passed our board's section
        if current_row >= end_row {
            break;
        }
    }

    Ok(config)
}

/// Check if a filename has .fseq extension (case insensitive)
/// Also handles 8.3 short names where .fseq becomes .FSE
pub fn is_fseq_file(name: &str) -> bool {
    let name = name.trim();
    if name.len() < 5 {
        return false;
    }
    // Skip hidden files (start with .)
    if name.starts_with('.') {
        return false;
    }
    // Skip macOS resource fork files (appear as _FILENAME in 8.3 format)
    if name.starts_with('_') {
        return false;
    }
    // Check for .fseq (long name) or .FSE (8.3 short name)
    if name.len() >= 5 {
        let ext = &name[name.len() - 5..];
        if ext.eq_ignore_ascii_case(".fseq") {
            return true;
        }
    }
    if name.len() >= 4 {
        let ext = &name[name.len() - 4..];
        if ext.eq_ignore_ascii_case(".fse") {
            return true;
        }
    }
    false
}

// ============================================================================
// Hardware-dependent SD card operations (firmware only)
// ============================================================================

#[cfg(feature = "firmware")]
pub mod hw {
    use super::*;
    use embassy_rp::gpio::Output;
    use embassy_rp::spi::Spi;
    use embassy_rp::peripherals::SPI0;
    use embassy_time::Delay;
    use embedded_sdmmc::{SdCard, TimeSource, Timestamp, VolumeIdx, VolumeManager, ShortFileName, LfnBuffer};
    use embedded_sdmmc::sdcard::AcquireOpts;
    use embedded_hal_bus::spi::ExclusiveDevice;
    use core::convert::Infallible;

    /// Dummy time source (we don't need real timestamps)
    struct DummyTimeSource;

    impl TimeSource for DummyTimeSource {
        fn get_timestamp(&self) -> Timestamp {
            Timestamp {
                year_since_1970: 55, // 2025
                zero_indexed_month: 0,
                zero_indexed_day: 0,
                hours: 12,
                minutes: 0,
                seconds: 0,
            }
        }
    }

    /// Type alias for the SPI device (no delay needed between transactions)
    type SpiDev<'a> = ExclusiveDevice<Spi<'a, SPI0, embassy_rp::spi::Async>, Output<'a>, embedded_hal_bus::spi::NoDelay>;

    /// Type alias for the SD card
    type SdCardType<'a> = SdCard<SpiDev<'a>, Delay>;

    /// SD card reader wrapping embedded-sdmmc
    pub struct SdReader<'a> {
        vm: VolumeManager<SdCardType<'a>, DummyTimeSource>,
    }

    /// Convert ShortFileName to a heapless String
    fn short_name_to_string(name: &ShortFileName) -> String<MAX_FILENAME_LEN> {
        let mut s: String<MAX_FILENAME_LEN> = String::new();

        // Get base name (up to 8 chars, space-padded)
        let base = name.base_name();
        for &b in base.iter() {
            if b == b' ' {
                break;
            }
            s.push(b as char).ok();
        }

        // Get extension (up to 3 chars, space-padded)
        let ext = name.extension();
        let has_ext = ext.iter().any(|&b| b != b' ');
        if has_ext {
            s.push('.').ok();
            for &b in ext.iter() {
                if b == b' ' {
                    break;
                }
                s.push(b as char).ok();
            }
        }

        s
    }

    impl<'a> SdReader<'a> {
        /// Create a new SD reader
        pub fn new(
            spi: Spi<'a, SPI0, embassy_rp::spi::Async>,
            cs: Output<'a>,
        ) -> Result<Self, SdError> {
            let spi_dev: SpiDev<'a> = ExclusiveDevice::new_no_delay(spi, cs)
                .map_err(|_: Infallible| SdError::NoCard)?;

            // Use minimal retries for fast failure when no card present
            // Default is 50 retries which takes ~10 seconds
            let opts = AcquireOpts {
                use_crc: true,
                acquire_retries: 1,
            };
            let sd_card = SdCard::new_with_options(spi_dev, Delay, opts);

            let vm = VolumeManager::new(sd_card, DummyTimeSource);

            Ok(Self { vm })
        }

        /// Load board config from config.csv
        pub fn load_config(&mut self, board_id: u8) -> Result<BoardConfig, SdError> {
            let volume = self.vm.open_volume(VolumeIdx(0)).map_err(|_| SdError::MountFailed)?;
            let root_dir = volume.open_root_dir().map_err(|_| SdError::MountFailed)?;

            let file = root_dir.open_file_in_dir("config.csv", embedded_sdmmc::Mode::ReadOnly)
                .map_err(|_| SdError::FileNotFound)?;

            // Read file into buffer (max 4KB)
            let mut buffer = [0u8; 4096];
            let bytes_read = file.read(&mut buffer).map_err(|_| SdError::ReadError)?;

            // Parse as UTF-8
            let content = core::str::from_utf8(&buffer[..bytes_read])
                .map_err(|_| SdError::ParseError)?;

            parse_config_buffer(content, board_id)
        }

        /// Scan root directory for .fseq files (with LFN support)
        pub fn scan_fseq_files(&mut self) -> Result<FileList, SdError> {
            let volume = self.vm.open_volume(VolumeIdx(0)).map_err(|_| SdError::MountFailed)?;
            let root_dir = volume.open_root_dir().map_err(|_| SdError::MountFailed)?;

            let mut files = FileList::default();

            // Buffer for long filename decoding
            let mut lfn_storage = [0u8; 256];
            let mut lfn_buffer = LfnBuffer::new(&mut lfn_storage);

            root_dir.iterate_dir_lfn(&mut lfn_buffer, |entry, lfn| {
                if files.count >= MAX_FSEQ_FILES as u8 {
                    return;
                }

                if entry.attributes.is_directory() {
                    return;
                }

                // Prefer long filename if available, otherwise use 8.3 short name
                let name_str = lfn.unwrap_or_else(|| {
                    // Fallback: we'll handle this below
                    ""
                });

                // Build the filename string
                let mut name: String<MAX_FILENAME_LEN> = String::new();
                if !name_str.is_empty() {
                    // Use the long filename
                    for c in name_str.chars().take(MAX_FILENAME_LEN) {
                        name.push(c).ok();
                    }
                } else {
                    // Use the short 8.3 name
                    name = short_name_to_string(&entry.name);
                }

                if is_fseq_file(&name) {
                    files.names[files.count as usize] = name;
                    files.count += 1;
                }
            }).map_err(|_| SdError::ReadError)?;

            Ok(files)
        }
    }
}

// ============================================================================
// Tests
// ============================================================================

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_color_order() {
        assert_eq!(ColorOrder::parse("RGB"), ColorOrder::RGB);
        assert_eq!(ColorOrder::parse("grb"), ColorOrder::GRB);
        assert_eq!(ColorOrder::parse("BGR"), ColorOrder::BGR);
        assert_eq!(ColorOrder::parse("  GRB  "), ColorOrder::GRB);
        assert_eq!(ColorOrder::parse("invalid"), ColorOrder::GRB); // default
    }

    #[test]
    fn parse_config_line_valid() {
        let config = parse_config_line("50,GRB").unwrap();
        assert_eq!(config.pixel_count, 50);
        assert_eq!(config.color_order, ColorOrder::GRB);

        let config = parse_config_line("100, RGB").unwrap();
        assert_eq!(config.pixel_count, 100);
        assert_eq!(config.color_order, ColorOrder::RGB);
    }

    #[test]
    fn parse_config_line_disabled() {
        let config = parse_config_line("0").unwrap();
        assert_eq!(config.pixel_count, 0);

        let config = parse_config_line("0,GRB").unwrap();
        assert_eq!(config.pixel_count, 0);
    }

    #[test]
    fn parse_config_line_comments() {
        assert!(parse_config_line("# comment").is_none());
        assert!(parse_config_line("").is_none());
        assert!(parse_config_line("  ").is_none());
    }

    #[test]
    fn parse_config_buffer_board0() {
        let csv = "50,GRB\n100,RGB\n0\n";
        let config = parse_config_buffer(csv, 0).unwrap();
        assert_eq!(config.strings[0].pixel_count, 50);
        assert_eq!(config.strings[0].color_order, ColorOrder::GRB);
        assert_eq!(config.strings[1].pixel_count, 100);
        assert_eq!(config.strings[1].color_order, ColorOrder::RGB);
        assert_eq!(config.strings[2].pixel_count, 0);
        assert_eq!(config.max_pixel_count, 100);
        assert_eq!(config.string_count, 2);
    }

    #[test]
    fn is_fseq_file_check() {
        assert!(is_fseq_file("test.fseq"));
        assert!(is_fseq_file("TEST.FSEQ"));
        assert!(is_fseq_file("my_show.fseq"));
        assert!(is_fseq_file("TEST.FSE")); // 8.3 short name
        assert!(is_fseq_file("MYSHOW.FSE")); // 8.3 short name
        assert!(!is_fseq_file(".fseq")); // Hidden
        assert!(!is_fseq_file("._test.fseq")); // macOS resource fork (long name)
        assert!(!is_fseq_file("_TEST.FSE")); // macOS resource fork (8.3 name)
        assert!(!is_fseq_file("test.txt"));
        assert!(!is_fseq_file("fseq"));
    }
}
