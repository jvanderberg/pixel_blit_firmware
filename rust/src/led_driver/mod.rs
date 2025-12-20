//! Parallel WS2812 LED driver for RP2350.
//!
//! Drives up to 32 LED strings in parallel using PIO.
//! Ported from the C pb_led_driver library.
//!
//! # Architecture
//!
//! Uses a safe token-based double-buffer design:
//! - **Runner task**: Holds WriteToken, generates frames, sends ReadToken to output
//! - **Output task**: Receives ReadToken, does DMA, returns WriteToken
//!
//! Channels enforce the ownership protocol - no unsafe code needed.
//! Double buffering lets the CPU prepare the next frame while DMA outputs the current frame.
//!
//! # Usage
//!
//! ```ignore
//! use led_driver::{buffer, run, run_output_loop, LedOutput, set_generator};
//!
//! // Initialize buffer system (returns first WriteToken)
//! let token = buffer::init();
//!
//! // Spawn the output task
//! let output = LedOutput::new_parallel_32(&mut common, sm0);
//! spawner.spawn(led_output_task(output, dma)).unwrap();
//!
//! // Spawn the runner task with the initial token
//! spawner.spawn(led_runner_task(token)).unwrap();
//!
//! // Later, activate a generator
//! set_generator(Some(&MY_GENERATOR));
//! ```

pub mod buffer;
pub mod color;

#[cfg(feature = "firmware")]
pub mod output;

#[cfg(feature = "firmware")]
pub mod runner;

// Re-export commonly used types
pub use buffer::{FrameData, FrameStringConfig, WriteToken};
pub use color::{hsv_to_rgb, Rgb};

#[cfg(feature = "firmware")]
pub use output::{run_output_loop, LedOutput};

#[cfg(feature = "firmware")]
pub use runner::{get_measured_fps, run, set_generator, Frame, FrameGenerator};
