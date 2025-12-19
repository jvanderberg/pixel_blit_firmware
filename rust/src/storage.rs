//! Flash settings persistence.
//!
//! Stores settings in the last 4KB sector of flash.
//! Compatible with the C firmware's flash_settings_t format.

use crate::state::AppState;

/// Flash size for RP2350 (4MB)
pub const FLASH_SIZE: usize = 4 * 1024 * 1024;

/// Magic number "PBSE" (Pixel Blit Settings)
const MAGIC: u32 = 0x50425345;

/// Current settings version
const VERSION: u32 = 3;

/// Settings structure matching C firmware layout.
/// Total size: 20 bytes (fits in one flash page)
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct FlashSettings {
    pub magic: u32,
    pub version: u32,
    pub brightness: u8,
    pub was_playing: u8,
    pub playing_index: u8,
    pub auto_loop: u8,
    pub board_id: u8,
    pub reserved: [u8; 3],
    pub crc: u32,
}

impl Default for FlashSettings {
    fn default() -> Self {
        Self {
            magic: MAGIC,
            version: VERSION,
            brightness: 10,
            was_playing: 0,
            playing_index: 0,
            auto_loop: 0,
            board_id: 0,
            reserved: [0; 3],
            crc: 0,
        }
    }
}

impl FlashSettings {
    /// Create settings from current app state
    pub fn from_state(state: &AppState) -> Self {
        let mut settings = Self {
            magic: MAGIC,
            version: VERSION,
            brightness: state.brightness_level,
            was_playing: if state.sd_card.is_playing { 1 } else { 0 },
            playing_index: state.sd_card.playing_index,
            auto_loop: if state.sd_card.auto_loop { 1 } else { 0 },
            board_id: state.board_id,
            reserved: [0; 3],
            crc: 0,
        };
        settings.crc = settings.calculate_crc();
        settings
    }

    /// Apply loaded settings to app state
    pub fn apply_to_state(&self, state: &mut AppState) {
        state.brightness_level = self.brightness.clamp(1, 10);
        state.board_id = self.board_id.min(15);
        state.sd_card.is_playing = false; // Don't auto-start yet
        state.sd_card.playing_index = self.playing_index.min(15);
        state.sd_card.auto_loop = self.auto_loop != 0;
    }

    /// Check if settings indicate playback should resume
    pub fn should_resume_playback(&self) -> bool {
        self.was_playing != 0
    }

    /// Calculate CRC32 over all fields except CRC itself
    fn calculate_crc(&self) -> u32 {
        let bytes = self.as_bytes_without_crc();
        crc32(bytes)
    }

    /// Get bytes of struct excluding CRC field (last 4 bytes)
    fn as_bytes_without_crc(&self) -> &[u8] {
        let ptr = self as *const Self as *const u8;
        let size = core::mem::size_of::<Self>() - 4; // Exclude CRC
        unsafe { core::slice::from_raw_parts(ptr, size) }
    }

    /// Validate settings
    pub fn is_valid(&self) -> bool {
        if self.magic != MAGIC {
            return false;
        }
        if self.version != VERSION {
            return false;
        }
        if self.brightness < 1 || self.brightness > 10 {
            return false;
        }
        if self.playing_index > 15 {
            return false;
        }
        if self.board_id > 15 {
            return false;
        }
        // Verify CRC
        self.crc == self.calculate_crc()
    }
}

/// CRC32 with polynomial 0xEDB88320 (same as C implementation)
fn crc32(data: &[u8]) -> u32 {
    let mut crc: u32 = 0xFFFFFFFF;
    for &byte in data {
        crc ^= byte as u32;
        for _ in 0..8 {
            crc = if crc & 1 != 0 {
                (crc >> 1) ^ 0xEDB88320
            } else {
                crc >> 1
            };
        }
    }
    !crc
}

// ============================================================================
// Firmware-only persistence
// ============================================================================

#[cfg(feature = "firmware")]
pub mod persist {
    use super::*;
    use defmt::info;
    use embassy_executor::Spawner;
    use embassy_rp::flash::{Blocking, Flash, ERASE_SIZE};
    use embassy_rp::peripherals::FLASH;
    use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
    use embassy_sync::channel::Channel;
    use embassy_time::Duration;

    /// Settings stored in last 4KB sector
    const SETTINGS_OFFSET: u32 = (FLASH_SIZE - 4096) as u32;

    /// Debounce delay before saving to flash
    const SAVE_DEBOUNCE: Duration = Duration::from_secs(2);

    /// Channel for save requests
    static SAVE_CHANNEL: Channel<CriticalSectionRawMutex, FlashSettings, 1> = Channel::new();

    /// Manages persistent state with automatic change detection and debounced saves.
    pub struct Settings {
        last: FlashSettings,
    }

    impl Settings {
        /// Initialize persistence: load state from flash and spawn save task.
        pub fn init(
            flash: Flash<'static, FLASH, Blocking, FLASH_SIZE>,
            spawner: &Spawner,
        ) -> (Self, AppState) {
            // Load from flash first (need mutable borrow)
            let mut flash = flash;
            let loaded = load_from_flash(&mut flash);

            // Spawn save task (takes ownership of flash)
            spawner.spawn(save_task(flash).unwrap());

            // Build initial state
            let mut state = AppState::new();
            let last = if let Some(settings) = loaded {
                settings.apply_to_state(&mut state);
                info!("Settings loaded from flash");
                settings
            } else {
                info!("Using default settings");
                FlashSettings::from_state(&state)
            };

            (Self { last }, state)
        }

        /// Save state if persistent fields changed (debounced).
        ///
        /// Call this after dispatching an action that changed state.
        /// Returns true if a save was triggered.
        pub fn save_state(&mut self, state: &AppState) -> bool {
            let current = FlashSettings::from_state(state);
            if current != self.last {
                self.last = current;
                SAVE_CHANNEL.try_send(current).ok();
                true
            } else {
                false
            }
        }
    }

    /// Background task that handles debounced flash writes.
    #[embassy_executor::task]
    async fn save_task(mut flash: Flash<'static, FLASH, Blocking, FLASH_SIZE>) {
        let mut last_saved: Option<FlashSettings> = None;

        loop {
            let mut pending = SAVE_CHANNEL.receive().await;

            // Debounce: keep taking newer settings until timeout
            loop {
                match embassy_time::with_timeout(SAVE_DEBOUNCE, SAVE_CHANNEL.receive()).await {
                    Ok(newer) => pending = newer,
                    Err(_) => {
                        if last_saved.map_or(true, |last| last != pending) {
                            save_to_flash(&mut flash, &pending);
                            last_saved = Some(pending);
                        }
                        break;
                    }
                }
            }
        }
    }

    fn load_from_flash(flash: &mut Flash<'_, FLASH, Blocking, FLASH_SIZE>) -> Option<FlashSettings> {
        let mut buffer = [0u8; core::mem::size_of::<FlashSettings>()];

        if flash.blocking_read(SETTINGS_OFFSET, &mut buffer).is_err() {
            info!("Flash read failed");
            return None;
        }

        // Interpret buffer as FlashSettings
        let settings: FlashSettings = unsafe { core::ptr::read(buffer.as_ptr() as *const _) };

        if settings.is_valid() {
            info!("Loaded settings: brightness={}, board_id={}",
                  settings.brightness, settings.board_id);
            Some(settings)
        } else {
            info!("No valid settings found in flash");
            None
        }
    }

    /// Save settings to flash (internal)
    fn save_to_flash(flash: &mut Flash<'_, FLASH, Blocking, FLASH_SIZE>, settings: &FlashSettings) {
        // Prepare page buffer (256 bytes, filled with 0xFF)
        let mut page = [0xFFu8; 256];
        let settings_bytes = unsafe {
            core::slice::from_raw_parts(
                settings as *const FlashSettings as *const u8,
                core::mem::size_of::<FlashSettings>(),
            )
        };
        page[..settings_bytes.len()].copy_from_slice(settings_bytes);

        // Erase sector (4KB)
        if let Err(_) = flash.blocking_erase(SETTINGS_OFFSET, SETTINGS_OFFSET + ERASE_SIZE as u32) {
            info!("Flash erase failed");
            return;
        }

        // Write page
        if let Err(_) = flash.blocking_write(SETTINGS_OFFSET, &page) {
            info!("Flash write failed");
            return;
        }

        info!("Settings saved to flash");
    }
}

// ============================================================================
// Tests
// ============================================================================

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc32_matches_c_implementation() {
        // Test vector: empty data should give specific CRC
        let data: [u8; 0] = [];
        assert_eq!(crc32(&data), 0x00000000);

        // Test with known data
        let data = b"123456789";
        assert_eq!(crc32(data), 0xCBF43926); // Standard CRC32 test vector
    }

    #[test]
    fn settings_default_is_valid() {
        let mut settings = FlashSettings::default();
        settings.crc = settings.calculate_crc();
        assert!(settings.is_valid());
    }

    #[test]
    fn settings_from_state_calculates_crc() {
        let state = AppState::default();
        let settings = FlashSettings::from_state(&state);
        assert!(settings.is_valid());
    }

    #[test]
    fn settings_detects_corruption() {
        let mut settings = FlashSettings::default();
        settings.crc = settings.calculate_crc();
        assert!(settings.is_valid());

        // Corrupt a field
        settings.brightness = 5;
        assert!(!settings.is_valid()); // CRC mismatch
    }

    #[test]
    fn settings_rejects_bad_magic() {
        let mut settings = FlashSettings::default();
        settings.magic = 0xDEADBEEF;
        settings.crc = settings.calculate_crc();
        assert!(!settings.is_valid());
    }

    #[test]
    fn settings_rejects_out_of_range() {
        let mut settings = FlashSettings::default();
        settings.brightness = 11; // Out of range
        settings.crc = settings.calculate_crc();
        assert!(!settings.is_valid());
    }

    #[test]
    fn roundtrip_state_to_settings() {
        use crate::state::{AppState, SdCardState};

        let mut state = AppState::default();
        state.brightness_level = 7;
        state.board_id = 3;
        state.sd_card = SdCardState {
            is_playing: true,
            playing_index: 5,
            auto_loop: true,
            ..SdCardState::default()
        };

        let settings = FlashSettings::from_state(&state);
        assert!(settings.is_valid());
        assert_eq!(settings.brightness, 7);
        assert_eq!(settings.board_id, 3);
        assert_eq!(settings.was_playing, 1);
        assert_eq!(settings.playing_index, 5);
        assert_eq!(settings.auto_loop, 1);

        // Apply back to fresh state
        let mut new_state = AppState::default();
        settings.apply_to_state(&mut new_state);
        assert_eq!(new_state.brightness_level, 7);
        assert_eq!(new_state.board_id, 3);
        assert_eq!(new_state.sd_card.playing_index, 5);
        assert_eq!(new_state.sd_card.auto_loop, true);
        // is_playing stays false until we explicitly resume
        assert_eq!(new_state.sd_card.is_playing, false);
        assert!(settings.should_resume_playback());
    }
}
