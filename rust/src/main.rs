#![no_std]
#![no_main]

mod usb;

use defmt::info;
use embassy_executor::Spawner;
use embassy_rp::flash::Flash;
use embassy_rp::gpio::{Input, Level, Output, Pull};
use embassy_rp::i2c::{self, Config as I2cConfig, I2c};
use embassy_rp::peripherals::{I2C1, SPI0, USB};
use embassy_rp::spi::{Config as SpiConfig, Spi};
use embassy_rp::Peri;
use embassy_rp::usb::InterruptHandler as UsbInterruptHandler;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use embassy_time::Timer;
use reducto::App;
use sh1106::Builder;
use {defmt_rtt as _, panic_probe as _};

// Import from library
use pixel_blit::action::Action;
use pixel_blit::display::Sh1106Display;
use pixel_blit::reducer::AppReducer;
use pixel_blit::sdcard::hw::SdReader;
use pixel_blit::sdcard::set_file_list;
use pixel_blit::state::{AppState, MenuEntry};
use pixel_blit::storage::persist::Settings;
use pixel_blit::views::RootView;

embassy_rp::bind_interrupts!(pub struct Irqs {
    I2C1_IRQ => i2c::InterruptHandler<I2C1>;
    USBCTRL_IRQ => UsbInterruptHandler<USB>;
});

// Channel for button actions
static ACTION_CHANNEL: Channel<CriticalSectionRawMutex, Action, 4> = Channel::new();

// ============================================================================
// Button Tasks (ISR-based via async edge detection)
// ============================================================================

#[embassy_executor::task]
async fn button_next_task(mut btn: Input<'static>) {
    loop {
        btn.wait_for_falling_edge().await;
        info!("Next button pressed (ISR)");
        ACTION_CHANNEL.send(Action::ButtonNext).await;
        // Debounce: wait before accepting next press
        Timer::after_millis(200).await;
    }
}

#[embassy_executor::task]
async fn button_select_task(mut btn: Input<'static>) {
    loop {
        btn.wait_for_falling_edge().await;
        info!("Select button pressed (ISR)");
        ACTION_CHANNEL.send(Action::ButtonSelect).await;
        // Debounce: wait before accepting next press
        Timer::after_millis(200).await;
    }
}

// ============================================================================
// SD Card Task
// ============================================================================

/// Channel to trigger SD card rescan
static SD_RESCAN_CHANNEL: Channel<CriticalSectionRawMutex, u8, 1> = Channel::new();

#[embassy_executor::task]
async fn sd_card_task(
    mut spi0: Peri<'static, SPI0>,
    mut clk: Peri<'static, embassy_rp::peripherals::PIN_38>,
    mut mosi: Peri<'static, embassy_rp::peripherals::PIN_39>,
    mut miso: Peri<'static, embassy_rp::peripherals::PIN_36>,
    mut cs_pin: Peri<'static, embassy_rp::peripherals::PIN_37>,
    mut dma0: Peri<'static, embassy_rp::peripherals::DMA_CH0>,
    mut dma1: Peri<'static, embassy_rp::peripherals::DMA_CH1>,
    initial_board_id: u8,
) {
    // Wait for USB and other tasks to start
    usb_println!("SD: waiting 1s before init...");
    Timer::after_millis(1000).await;

    let mut board_id = initial_board_id;
    let mut config_loaded = false;

    loop {
        usb_println!("SD: creating SdReader...");

        // Create SPI fresh each attempt
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

        // Try to initialize SD card
        match SdReader::new(spi, cs) {
            Ok(mut reader) => {
                usb_println!("SD: card detected");

                // Load config.csv only once at boot
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
                            config_loaded = true; // Don't retry on rescan
                        }
                    }
                }

                // Scan for .fseq files
                usb_println!("SD: scanning for .fseq files...");
                match reader.scan_fseq_files() {
                    Ok(files) => {
                        let count = files.count;
                        usb_println!("SD: found {} files", count);
                        // Store in static buffer
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
            }
        }
        // SPI/CS dropped here, peripherals can be reused

        // Wait for rescan request
        board_id = SD_RESCAN_CHANNEL.receive().await;
        usb_println!("SD: rescan requested for board {}", board_id);
    }
}

// ============================================================================
// Main
// ============================================================================

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_rp::init(Default::default());

    // USB CDC first
    usb::init(&spawner, p.USB);

    // Wait for USB to connect so we can see boot logs
    Timer::after_millis(2000).await;

    info!("Pixel Blit Firmware starting...");
    usb_println!("Pixel Blit Firmware starting...");

    // Initialize persistence (loads state, spawns save task)
    let flash = Flash::new_blocking(p.FLASH);
    let (mut settings, state) = Settings::init(flash, &spawner);
    usb_println!("Settings loaded: brightness={}", state.brightness_level);

    // Set up buttons (active low with pull-up) and spawn ISR tasks
    let btn_next = Input::new(p.PIN_45, Pull::Up);
    let btn_select = Input::new(p.PIN_43, Pull::Up);
    spawner.spawn(button_next_task(btn_next).unwrap());
    spawner.spawn(button_select_task(btn_select).unwrap());

    // SPI0 for SD card on GPIO 36-39
    // MISO=36, CS=37, SCK=38, MOSI=39
    spawner.spawn(sd_card_task(
        p.SPI0,
        p.PIN_38, // SCK
        p.PIN_39, // MOSI
        p.PIN_36, // MISO
        p.PIN_37, // CS
        p.DMA_CH0,
        p.DMA_CH1,
        state.board_id,
    ).unwrap());
    usb_println!("SD card task spawned");

    // I2C1 on GPIO 46 (SDA) and GPIO 47 (SCL)
    let sda = p.PIN_46;
    let scl = p.PIN_47;
    let mut i2c_config = I2cConfig::default();
    i2c_config.frequency = 400_000;
    let i2c = I2c::new_async(p.I2C1, scl, sda, Irqs, i2c_config);

    // Initialize display
    let graphics_mode = Builder::new().connect_i2c(i2c).into();
    let mut display = Sh1106Display::new(graphics_mode);
    display.init().ok();
    let view = RootView::new(display);

    // Create app with view and loaded state
    let mut app: App<AppState, Action, AppReducer, _> = App::new(view, state);
    app.dispatch(Action::Boot);

    usb_println!("Entering main loop");

    // Main loop
    loop {
        let action = ACTION_CHANNEL.receive().await;
        let result = app.dispatch(action);

        if result.changed {
            // Save settings
            if settings.save_state(result.new) {
                usb_println!("Saving: brightness={}, board={}",
                    result.new.brightness_level, result.new.board_id);
            }

            // Trigger rescan when entering SD card menu
            let entered_sd = result.new.in_detail_view
                && result.new.menu_selection == MenuEntry::SdCard
                && !result.old.in_detail_view;
            if entered_sd {
                usb_println!("SD: menu entered, triggering rescan");
                SD_RESCAN_CHANNEL.try_send(result.new.board_id).ok();
            }
        }
    }
}
