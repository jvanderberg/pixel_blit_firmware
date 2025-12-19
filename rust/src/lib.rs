//! Pixel Blit Firmware - Core library
//!
//! This library contains the state management, actions, and reducer logic.
//! It is `no_std` compatible but can be tested on host.

#![cfg_attr(not(test), no_std)]

pub mod state;
pub mod action;
pub mod reducer;
pub mod storage;
pub mod sdcard;

// Display abstraction - always available, mock impl only in tests
#[cfg(feature = "view-test")]
pub mod display;

// Views require view-test feature (embedded-graphics)
#[cfg(feature = "view-test")]
pub mod views;

#[cfg(all(test, feature = "view-test"))]
mod view_tests;

#[cfg(test)]
mod reducer_tests;
