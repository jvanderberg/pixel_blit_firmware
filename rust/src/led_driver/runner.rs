//! High-level LED driver with swappable frame generators.
//!
//! The runner loop is always active but generators can be swapped at runtime.
//! When no generator is set, the driver idles (no DMA output).
//!
//! # Architecture
//!
//! Uses a token-based ownership system:
//! - Runner holds a `WriteToken`, writes pixels directly to the buffer
//! - When frame is ready, converts to `ReadToken` and sends to output
//! - Output does DMA, then returns a `WriteToken` for the next buffer
//!
//! # Example
//!
//! ```ignore
//! // Define a generator
//! static RAINBOW: RainbowGenerator = RainbowGenerator::new();
//!
//! // Initialize buffer system (returns first WriteToken)
//! let token = buffer::init();
//!
//! // Spawn the runner task with the initial token
//! spawner.spawn(led_runner_task(token)).unwrap();
//!
//! // Later, activate the generator
//! led_driver::set_generator(Some(&RAINBOW));
//! ```

use core::cell::RefCell;
use core::sync::atomic::{AtomicU16, Ordering};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::blocking_mutex::Mutex;
use embassy_time::{Duration, Instant, Timer};

use super::buffer::{
    FrameData, FrameStringConfig, WriteToken,
    FRAME_CHANNEL, FREE_CHANNEL,
    FSEQ_REQUEST_CHANNEL, FSEQ_TOKEN_CHANNEL, FSEQ_RETURN_CHANNEL,
};

/// Current frame generator (protected by mutex for atomic swap)
static CURRENT_GENERATOR: Mutex<CriticalSectionRawMutex, RefCell<Option<&'static dyn FrameGenerator>>> =
    Mutex::new(RefCell::new(None));

/// Measured FPS (updated every second by runner)
static MEASURED_FPS: AtomicU16 = AtomicU16::new(0);

/// Get the current measured FPS.
///
/// Returns the actual FPS achieved by the active generator.
/// Returns 0 when no generator is active.
pub fn get_measured_fps() -> u16 {
    MEASURED_FPS.load(Ordering::Relaxed)
}

/// Trait for frame generators.
///
/// Implement this for each animation type. Generators are stateless
/// or manage their own state internally via atomics.
pub trait FrameGenerator: Sync {
    /// Generate a frame. Called once per frame when active.
    fn generate(&self, frame: &mut Frame<'_>);

    /// Target FPS for this generator.
    fn target_fps(&self) -> u16 {
        60
    }
}

/// Set the current frame generator.
///
/// Pass `None` to stop output and idle.
/// Pass `Some(&generator)` to activate a generator.
pub fn set_generator(gen: Option<&'static dyn FrameGenerator>) {
    CURRENT_GENERATOR.lock(|cell| {
        *cell.borrow_mut() = gen;
    });
}

/// Get the current generator (if any).
fn current_generator() -> Option<&'static dyn FrameGenerator> {
    CURRENT_GENERATOR.lock(|cell| *cell.borrow())
}

/// Frame handle passed to generators.
///
/// Provides safe access to the frame buffer for writing pixels.
pub struct Frame<'a> {
    data: &'a mut FrameData,
}

impl<'a> Frame<'a> {
    /// Set a pixel color for a specific string.
    #[inline]
    pub fn set_pixel(&mut self, string: u8, pixel: u16, color: super::Rgb) {
        self.data.set_pixel(string, pixel, color);
    }

    /// Clear all pixels to black.
    pub fn clear(&mut self) {
        self.data.clear();
    }

    /// Set all strings to same length and color order.
    pub fn set_uniform_config(&mut self, length: u16, color_order: crate::state::ColorOrder) {
        self.data.set_uniform_config(length, color_order);
    }

    /// Configure a single string.
    pub fn set_string_config(&mut self, string: u8, config: FrameStringConfig) {
        self.data.set_string_config(string, config);
    }

    /// Get the max pixel count across all strings.
    pub fn max_length(&self) -> u16 {
        self.data.max_length()
    }

    /// Get config for a specific string.
    pub fn string_config(&self, string: u8) -> FrameStringConfig {
        self.data.string_config(string)
    }
}

/// Run the LED driver loop.
///
/// This function runs forever, checking for an active generator each iteration.
/// When a generator is set, it generates frames at the target FPS.
/// When no generator is set, it idles without DMA output.
///
/// # Arguments
/// * `initial_token` - The initial WriteToken from `buffer::init()`
pub async fn run(initial_token: WriteToken) -> ! {
    let mut token = initial_token;
    let mut last_frame_time: Option<Instant> = None;
    let mut was_active = false;
    let mut frame_count: u32 = 0;

    // FPS measurement
    let mut fps_frame_count: u32 = 0;
    let mut fps_last_time: Option<Instant> = None;

    loop {
        // Check if FSEQ task wants to take over
        if FSEQ_REQUEST_CHANNEL.try_receive().is_ok() {
            defmt::info!("Runner: FSEQ takeover requested");

            // Hand off our token
            FSEQ_TOKEN_CHANNEL.send(token).await;

            // Wait for token to be returned
            token = FSEQ_RETURN_CHANNEL.receive().await;
            defmt::info!("Runner: FSEQ returned token, resuming");

            // Reset timing state
            last_frame_time = None;
            fps_last_time = None;
            was_active = false;
            continue;
        }

        if let Some(gen) = current_generator() {
            if !was_active {
                defmt::info!("Runner: generator detected, starting");
                fps_frame_count = 0;
                fps_last_time = Some(Instant::now());
            }

            // Generator active - produce frames
            let frame_interval = Duration::from_micros(1_000_000 / gen.target_fps() as u64);

            // Get direct mutable access to buffer via token
            {
                let data = token.get_mut();
                let mut frame = Frame { data };
                gen.generate(&mut frame);
            }

            frame_count = frame_count.wrapping_add(1);
            fps_frame_count += 1;

            // Update FPS measurement every second
            if let Some(fps_start) = fps_last_time {
                let elapsed = Instant::now().duration_since(fps_start);
                if elapsed.as_millis() >= 1000 {
                    let fps = (fps_frame_count as u64 * 1000) / elapsed.as_millis();
                    MEASURED_FPS.store(fps as u16, Ordering::Relaxed);
                    fps_frame_count = 0;
                    fps_last_time = Some(Instant::now());
                }
            }

            // FPS limiting - wait before sending frame
            let target_time = if let Some(last) = last_frame_time {
                last + frame_interval
            } else {
                Instant::now()
            };
            let now = Instant::now();
            if now < target_time {
                Timer::after(target_time - now).await;
            }
            last_frame_time = Some(target_time); // Use target, not actual time (drift correction)

            // Convert WriteToken to ReadToken and send to output task
            let read_token = token.into_read_token();
            defmt::trace!("Runner: sending frame to output");
            FRAME_CHANNEL.send(read_token).await;

            // Wait for a free buffer from output task
            defmt::trace!("Runner: waiting for free buffer");
            token = FREE_CHANNEL.receive().await;
            defmt::trace!("Runner: got buffer {}", token.index());

            was_active = true;
        } else {
            // No generator - idle
            if was_active {
                defmt::info!("Runner: generator removed, sending clear frame");

                // Send one clear frame
                token.get_mut().clear();

                let read_token = token.into_read_token();
                FRAME_CHANNEL.send(read_token).await;
                token = FREE_CHANNEL.receive().await;

                was_active = false;
                frame_count = 0;
                MEASURED_FPS.store(0, Ordering::Relaxed);
            }
            last_frame_time = None;
            fps_last_time = None;
            Timer::after_millis(50).await;
        }
    }
}
