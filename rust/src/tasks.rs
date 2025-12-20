//! Async tasks for the firmware.

use defmt::info;
use embassy_rp::gpio::{Input, Level, Output};
use embassy_rp::peripherals::{PIO1, SPI0};
use embassy_rp::pio::Pio;
use embassy_rp::spi::{Config as SpiConfig, Spi};
use embassy_rp::Peri;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use embassy_time::Timer;

use pixel_blit::action::Action;
use pixel_blit::led_driver::{self, WriteToken};
use pixel_blit::sdcard::hw::SdReader;
use pixel_blit::sdcard::set_file_list;

use crate::{usb_println, Irqs};

/// Channel for dispatching actions to the main loop.
pub static ACTION_CHANNEL: Channel<CriticalSectionRawMutex, Action, 4> = Channel::new();

/// Channel to trigger SD card rescan.
pub static SD_RESCAN_CHANNEL: Channel<CriticalSectionRawMutex, u8, 1> = Channel::new();

// ============================================================================
// Button Tasks
// ============================================================================

#[embassy_executor::task]
pub async fn button_next_task(mut btn: Input<'static>) {
    loop {
        btn.wait_for_falling_edge().await;
        info!("Next button pressed");
        ACTION_CHANNEL.send(Action::ButtonNext).await;
        Timer::after_millis(200).await;
    }
}

#[embassy_executor::task]
pub async fn button_select_task(mut btn: Input<'static>) {
    loop {
        btn.wait_for_falling_edge().await;
        info!("Select button pressed");
        ACTION_CHANNEL.send(Action::ButtonSelect).await;
        Timer::after_millis(200).await;
    }
}

// ============================================================================
// Timer Task
// ============================================================================

#[embassy_executor::task]
pub async fn tick_task() {
    loop {
        Timer::after_secs(1).await;
        ACTION_CHANNEL.send(Action::Tick1s).await;
        let fps = led_driver::get_measured_fps();
        ACTION_CHANNEL.send(Action::RainbowFrameComplete { fps }).await;
    }
}

// ============================================================================
// SD Card Task
// ============================================================================

#[embassy_executor::task]
pub async fn sd_card_task(
    mut spi0: Peri<'static, SPI0>,
    mut clk: Peri<'static, embassy_rp::peripherals::PIN_38>,
    mut mosi: Peri<'static, embassy_rp::peripherals::PIN_39>,
    mut miso: Peri<'static, embassy_rp::peripherals::PIN_36>,
    mut cs_pin: Peri<'static, embassy_rp::peripherals::PIN_37>,
    mut dma0: Peri<'static, embassy_rp::peripherals::DMA_CH0>,
    mut dma1: Peri<'static, embassy_rp::peripherals::DMA_CH1>,
    initial_board_id: u8,
) {
    usb_println!("SD: waiting 1s before init...");
    Timer::after_millis(1000).await;

    let mut board_id = initial_board_id;
    let mut config_loaded = false;

    loop {
        usb_println!("SD: creating SdReader...");

        let mut spi_config = SpiConfig::default();
        spi_config.frequency = 400_000;
        let spi = Spi::new(
            spi0.reborrow(),
            clk.reborrow(),
            mosi.reborrow(),
            miso.reborrow(),
            dma0.reborrow(),
            dma1.reborrow(),
            spi_config,
        );
        let cs = Output::new(cs_pin.reborrow(), Level::High);

        match SdReader::new(spi, cs) {
            Ok(mut reader) => {
                usb_println!("SD: card detected");

                if !config_loaded {
                    usb_println!("SD: loading config.csv for board {}...", board_id);
                    match reader.load_config(board_id) {
                        Ok(config) => {
                            usb_println!("SD: config loaded - {} strings, max {} px",
                                config.string_count, config.max_pixel_count);
                            ACTION_CHANNEL.send(Action::SdConfigLoaded {
                                strings: config.strings,
                                string_count: config.string_count,
                                max_pixel_count: config.max_pixel_count,
                            }).await;
                            config_loaded = true;
                        }
                        Err(e) => {
                            usb_println!("SD: config error: {}", e.message());
                            ACTION_CHANNEL.send(Action::SdConfigError {
                                message: "No config.csv - defaults",
                            }).await;
                            config_loaded = true;
                        }
                    }
                }

                usb_println!("SD: scanning for .fseq files...");
                match reader.scan_fseq_files() {
                    Ok(files) => {
                        let count = files.count;
                        usb_println!("SD: found {} files", count);
                        set_file_list(files);
                        ACTION_CHANNEL.send(Action::SdFilesScanned { count }).await;
                    }
                    Err(e) => {
                        usb_println!("SD: scan error: {}", e.message());
                        ACTION_CHANNEL.send(Action::SdCardError {
                            message: e.message(),
                        }).await;
                    }
                }
                usb_println!("SD: scan complete");
            }
            Err(e) => {
                usb_println!("SD: init FAILED: {}", e.message());
                ACTION_CHANNEL.send(Action::SdCardError {
                    message: e.message(),
                }).await;
                Timer::after_millis(500).await;
            }
        }

        board_id = SD_RESCAN_CHANNEL.receive().await;
        usb_println!("SD: rescan requested for board {}", board_id);
    }
}

// ============================================================================
// LED Tasks
// ============================================================================

#[embassy_executor::task]
pub async fn led_output_task(
    pio1: Peri<'static, PIO1>,
    dma: Peri<'static, embassy_rp::peripherals::DMA_CH2>,
) {
    Timer::after_millis(100).await;
    usb_println!("LED Output: initializing PIO1...");

    let Pio { mut common, sm0, .. } = Pio::new(pio1, Irqs);

    usb_println!("LED Output: configuring 32 parallel outputs...");
    let output = led_driver::LedOutput::new_parallel_32(&mut common, sm0);
    usb_println!("LED Output: running output loop");

    led_driver::run_output_loop(output, dma).await
}

#[embassy_executor::task]
pub async fn led_runner_task(token: WriteToken) {
    usb_println!("LED Runner: starting");
    Timer::after_millis(100).await;
    usb_println!("LED Runner: entering run loop");
    led_driver::run(token).await
}
