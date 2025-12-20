//! Parallel WS2812 LED driver for RP2350.
//!
//! Drives up to 32 LED strings in parallel using PIO.
//! Ported from the C pb_led_driver library.
//!
//! # Architecture
//!
//! Uses a two-task design for true async DMA:
//! - **Writer task**: Generates frames, writes to back buffer
//! - **Output task**: Swaps buffers, handles DMA
//!
//! Double buffering lets the CPU prepare the next frame while
//! DMA outputs the current frame.
//!
//! # Usage
//!
//! ```ignore
//! use led_driver::{run, run_output_loop, LedOutput, Rgb, hsv_to_rgb};
//!
//! // In main: spawn the output task
//! let output = LedOutput::new(&mut common, sm0, pins...);
//! spawner.spawn(led_output_task(output, dma)).unwrap();
//!
//! // Run the frame generator
//! run(60, |frame| {
//!     frame.set_pixel(0, 0, Rgb::RED);
//! }).await;
//! ```

pub mod buffer;
pub mod color;

#[cfg(feature = "firmware")]
pub mod output;

#[cfg(feature = "firmware")]
pub mod runner;

pub use buffer::{LedBuffer, StringConfig};
pub use color::{hsv_to_rgb, Rgb};

#[cfg(feature = "firmware")]
pub use output::{run_output_loop, LedOutput};

#[cfg(feature = "firmware")]
pub use runner::{run, set_generator, Frame, FrameGenerator};
