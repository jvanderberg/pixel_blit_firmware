//! High-level LED driver with swappable frame generators.
//!
//! The runner loop is always active but generators can be swapped at runtime.
//! When no generator is set, the driver idles (no DMA output).
//!
//! # Example
//!
//! ```ignore
//! // Define a generator
//! static RAINBOW: RainbowGenerator = RainbowGenerator::new();
//!
//! // Start the runner (spawns as a task)
//! spawner.spawn(led_runner_task()).unwrap();
//!
//! // Later, activate the generator
//! led_driver::set_generator(Some(&RAINBOW));
//!
//! // When done, deactivate
//! led_driver::set_generator(None);
//! ```

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::blocking_mutex::Mutex;
use embassy_sync::signal::Signal;
use embassy_time::{Duration, Instant, Timer};

use core::cell::RefCell;

use super::buffer::{LedBuffer, StringConfig};

/// Signal from writer to DMA task: frame is ready to show
static FRAME_READY: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// Signal from DMA task to writer: back buffer is free
static BUFFER_FREE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// Shared LED buffer (static for cross-task access)
static mut LED_BUFFER: LedBuffer = LedBuffer::new();

/// Current frame generator (protected by mutex for atomic swap)
static CURRENT_GENERATOR: Mutex<CriticalSectionRawMutex, RefCell<Option<&'static dyn FrameGenerator>>> =
    Mutex::new(RefCell::new(None));

/// Trait for frame generators.
///
/// Implement this for each animation type. Generators are stateless
/// or manage their own state internally.
pub trait FrameGenerator: Sync {
    /// Generate a frame. Called once per frame when active.
    fn generate(&self, frame: &mut Frame);

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
/// Provides safe access to the back buffer for writing pixels.
pub struct Frame<'a> {
    buffer: &'a mut LedBuffer,
}

impl<'a> Frame<'a> {
    /// Set a pixel color for a specific string.
    #[inline]
    pub fn set_pixel(&mut self, string: u8, pixel: u16, color: super::Rgb) {
        self.buffer.set_pixel(string, pixel, color);
    }

    /// Clear all pixels to black.
    pub fn clear(&mut self) {
        self.buffer.clear();
    }

    /// Set all strings to same length and color order.
    pub fn set_uniform_config(&mut self, length: u16, color_order: crate::state::ColorOrder) {
        self.buffer.set_uniform_config(length, color_order);
    }

    /// Configure a single string.
    pub fn set_string_config(&mut self, string: u8, config: StringConfig) {
        self.buffer.set_string_config(string, config);
    }

    /// Get the max pixel count across all strings.
    pub fn max_length(&self) -> u16 {
        self.buffer.max_length()
    }

    /// Get config for a specific string.
    pub fn string_config(&self, string: u8) -> StringConfig {
        self.buffer.string_config(string)
    }
}

/// Get access to the shared buffer for the DMA task.
///
/// # Safety
/// Only call from the DMA task after FRAME_READY signal.
pub(super) unsafe fn get_buffer() -> &'static mut LedBuffer {
    &mut *core::ptr::addr_of_mut!(LED_BUFFER)
}

/// Signal that the frame is ready (called by runner).
pub(super) fn signal_frame_ready() {
    FRAME_READY.signal(());
}

/// Wait for frame ready signal (called by DMA task).
pub(super) async fn wait_frame_ready() {
    FRAME_READY.wait().await;
}

/// Signal that buffer is free (called by DMA task).
pub(super) fn signal_buffer_free() {
    BUFFER_FREE.signal(());
}

/// Wait for buffer free signal (called by runner).
pub(super) async fn wait_buffer_free() {
    BUFFER_FREE.wait().await;
}

/// Run the LED driver loop.
///
/// This function runs forever, checking for an active generator each iteration.
/// When a generator is set, it generates frames at the target FPS.
/// When no generator is set, it idles without DMA output.
pub async fn run() -> ! {

    let mut last_frame_time: Option<Instant> = None;
    let mut was_active = false;
    let mut frame_count: u32 = 0;

    loop {
        if let Some(gen) = current_generator() {
            if !was_active {
                defmt::info!("Runner: generator detected, starting");
            }

            // Generator active - produce frames
            let frame_interval = Duration::from_micros(1_000_000 / gen.target_fps() as u64);

            // Wait for back buffer to be free
            defmt::trace!("Runner: waiting for buffer");
            wait_buffer_free().await;
            defmt::trace!("Runner: buffer free");

            // Get access to buffer and create frame handle
            let buffer = unsafe { get_buffer() };
            let mut frame = Frame { buffer };

            // Call generator
            gen.generate(&mut frame);

            frame_count = frame_count.wrapping_add(1);
            if frame_count % 600 == 0 {
                defmt::info!("Runner: frame {}", frame_count);
            }

            // FPS limiting
            if let Some(last) = last_frame_time {
                let target = last + frame_interval;
                let now = Instant::now();
                if now < target {
                    Timer::after(target - now).await;
                }
            }
            last_frame_time = Some(Instant::now());

            // Signal DMA task
            defmt::trace!("Runner: signaling frame ready");
            signal_frame_ready();
            was_active = true;
        } else {
            // No generator - idle
            if was_active {
                defmt::info!("Runner: generator removed, clearing");
                // Clear LEDs on transition to idle
                wait_buffer_free().await;
                let buffer = unsafe { get_buffer() };
                buffer.clear();
                signal_frame_ready();
                was_active = false;
                frame_count = 0;
            }
            last_frame_time = None;
            Timer::after_millis(50).await;
        }
    }
}
