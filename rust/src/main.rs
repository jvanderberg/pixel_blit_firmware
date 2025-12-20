#![no_std]
#![no_main]

mod usb;

use core::sync::atomic::{AtomicU8, AtomicU16, Ordering};
use defmt::info;
use embassy_executor::Spawner;
use embassy_rp::flash::Flash;
use embassy_rp::gpio::{Input, Level, Output, Pull};
use embassy_rp::i2c::{self, Config as I2cConfig, I2c};
use embassy_rp::peripherals::{I2C1, PIO1, SPI0, USB};
use embassy_rp::pio::{InterruptHandler as PioInterruptHandler, Pio};
use embassy_rp::spi::{Config as SpiConfig, Spi};
use embassy_rp::usb::InterruptHandler as UsbInterruptHandler;
use embassy_rp::Peri;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use embassy_time::Timer;
use reducto::App;
use sh1106::Builder;
use {defmt_rtt as _, panic_probe as _};

// Import from library
use pixel_blit::action::Action;
use pixel_blit::display::Sh1106Display;
use pixel_blit::led_driver::{self, Frame, FrameGenerator};
use pixel_blit::reducer::AppReducer;
use pixel_blit::sdcard::hw::SdReader;
use pixel_blit::sdcard::set_file_list;
use pixel_blit::state::{AppState, MenuEntry, RunState};
use pixel_blit::storage::persist::Settings;
use pixel_blit::views::RootView;

embassy_rp::bind_interrupts!(pub struct Irqs {
    I2C1_IRQ => i2c::InterruptHandler<I2C1>;
    USBCTRL_IRQ => UsbInterruptHandler<USB>;
    PIO1_IRQ_0 => PioInterruptHandler<PIO1>;
});

// Channel for button actions
static ACTION_CHANNEL: Channel<CriticalSectionRawMutex, Action, 4> = Channel::new();

// ============================================================================
// Frame Generators (independent, swappable at runtime)
// ============================================================================

/// Rainbow test generator - displays rainbow on selected string, red on others.
pub struct RainbowGenerator {
    current_string: AtomicU8,
    hue_offset: AtomicU8,
}

impl RainbowGenerator {
    pub const fn new() -> Self {
        Self {
            current_string: AtomicU8::new(0),
            hue_offset: AtomicU8::new(0),
        }
    }

    pub fn set_string(&self, string: u8) {
        self.current_string.store(string, Ordering::Relaxed);
    }
}

impl FrameGenerator for RainbowGenerator {
    fn generate(&self, frame: &mut Frame) {
        const PIXELS: u16 = 50;
        // Rainbow test uses 50 pixels, RGB order
        frame.set_uniform_config(PIXELS, pixel_blit::state::ColorOrder::RGB);

        let selected = self.current_string.load(Ordering::Relaxed);
        let hue = self.hue_offset.fetch_add(1, Ordering::Relaxed);

        for string in 0..32u8 {
            for pixel in 0..PIXELS {
                let color = if string == selected {
                    let h = hue.wrapping_add((pixel as u8).wrapping_mul(5));
                    led_driver::hsv_to_rgb(h, 255, 255)
                } else {
                    led_driver::Rgb::RED
                };
                frame.set_pixel(string, pixel, color);
            }
        }
    }
}

/// String length generator - lights a single pixel on selected string.
pub struct StringLengthGenerator {
    current_string: AtomicU8,
    current_pixel: AtomicU16,
}

impl StringLengthGenerator {
    pub const fn new() -> Self {
        Self {
            current_string: AtomicU8::new(0),
            current_pixel: AtomicU16::new(1),
        }
    }

    pub fn set_position(&self, string: u8, pixel: u16) {
        self.current_string.store(string, Ordering::Relaxed);
        self.current_pixel.store(pixel, Ordering::Relaxed);
    }
}

impl FrameGenerator for StringLengthGenerator {
    fn generate(&self, frame: &mut Frame) {
        let string = self.current_string.load(Ordering::Relaxed);
        let pixel = self.current_pixel.load(Ordering::Relaxed);

        // String length test uses RGB order
        frame.set_uniform_config(pixel + 1, pixel_blit::state::ColorOrder::RGB);
        frame.clear();
        frame.set_pixel(string, pixel, led_driver::Rgb::RED);
    }

    fn target_fps(&self) -> u16 {
        30 // Lower FPS for static display
    }
}

// Static generator instances
static RAINBOW_GENERATOR: RainbowGenerator = RainbowGenerator::new();
static STRING_LENGTH_GENERATOR: StringLengthGenerator = StringLengthGenerator::new();

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
// Rainbow Test Task
// ============================================================================


/// Struct to hold all 32 LED output pins
pub struct LedPins {
    pub pin0: Peri<'static, embassy_rp::peripherals::PIN_0>,
    pub pin1: Peri<'static, embassy_rp::peripherals::PIN_1>,
    pub pin2: Peri<'static, embassy_rp::peripherals::PIN_2>,
    pub pin3: Peri<'static, embassy_rp::peripherals::PIN_3>,
    pub pin4: Peri<'static, embassy_rp::peripherals::PIN_4>,
    pub pin5: Peri<'static, embassy_rp::peripherals::PIN_5>,
    pub pin6: Peri<'static, embassy_rp::peripherals::PIN_6>,
    pub pin7: Peri<'static, embassy_rp::peripherals::PIN_7>,
    pub pin8: Peri<'static, embassy_rp::peripherals::PIN_8>,
    pub pin9: Peri<'static, embassy_rp::peripherals::PIN_9>,
    pub pin10: Peri<'static, embassy_rp::peripherals::PIN_10>,
    pub pin11: Peri<'static, embassy_rp::peripherals::PIN_11>,
    pub pin12: Peri<'static, embassy_rp::peripherals::PIN_12>,
    pub pin13: Peri<'static, embassy_rp::peripherals::PIN_13>,
    pub pin14: Peri<'static, embassy_rp::peripherals::PIN_14>,
    pub pin15: Peri<'static, embassy_rp::peripherals::PIN_15>,
    pub pin16: Peri<'static, embassy_rp::peripherals::PIN_16>,
    pub pin17: Peri<'static, embassy_rp::peripherals::PIN_17>,
    pub pin18: Peri<'static, embassy_rp::peripherals::PIN_18>,
    pub pin19: Peri<'static, embassy_rp::peripherals::PIN_19>,
    pub pin20: Peri<'static, embassy_rp::peripherals::PIN_20>,
    pub pin21: Peri<'static, embassy_rp::peripherals::PIN_21>,
    pub pin22: Peri<'static, embassy_rp::peripherals::PIN_22>,
    pub pin23: Peri<'static, embassy_rp::peripherals::PIN_23>,
    pub pin24: Peri<'static, embassy_rp::peripherals::PIN_24>,
    pub pin25: Peri<'static, embassy_rp::peripherals::PIN_25>,
    pub pin26: Peri<'static, embassy_rp::peripherals::PIN_26>,
    pub pin27: Peri<'static, embassy_rp::peripherals::PIN_27>,
    pub pin28: Peri<'static, embassy_rp::peripherals::PIN_28>,
    pub pin29: Peri<'static, embassy_rp::peripherals::PIN_29>,
    pub pin30: Peri<'static, embassy_rp::peripherals::PIN_30>,
    pub pin31: Peri<'static, embassy_rp::peripherals::PIN_31>,
}

/// LED output task - owns PIO and DMA, handles buffer swapping and output.
#[embassy_executor::task]
async fn led_output_task(
    pio1: Peri<'static, PIO1>,
    dma: Peri<'static, embassy_rp::peripherals::DMA_CH2>,
    pins: LedPins,
) {
    // Let USB enumerate before doing anything
    Timer::after_millis(100).await;
    usb_println!("LED Output: initializing PIO1...");

    // Initialize PIO1 for LED output
    let Pio {
        mut common, sm0, ..
    } = Pio::new(pio1, Irqs);

    usb_println!("LED Output: Pio::new done, configuring 32 parallel outputs...");

    // Use direct register access for 32 pins (avoids stack overflow)
    let output = led_driver::LedOutput::new_parallel_32(&mut common, sm0);
    let _ = pins;  // pins are configured directly via registers
    usb_println!("LED Output: running output loop");

    // Run the output loop forever
    led_driver::run_output_loop(output, dma).await
}

/// LED runner task - runs the generator loop.
/// Generators are swapped via led_driver::set_generator() from main loop.
#[embassy_executor::task]
async fn led_runner_task() {
    usb_println!("LED Runner: starting");
    Timer::after_millis(100).await;
    usb_println!("LED Runner: entering run loop");
    led_driver::run().await
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
    info!("Spawning button tasks...");
    let btn_next = Input::new(p.PIN_45, Pull::Up);
    let btn_select = Input::new(p.PIN_43, Pull::Up);
    spawner.spawn(button_next_task(btn_next).unwrap());
    spawner.spawn(button_select_task(btn_select).unwrap());
    info!("Button tasks spawned");

    // SPI0 for SD card on GPIO 36-39
    // MISO=36, CS=37, SCK=38, MOSI=39
    info!("Spawning SD card task...");
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
    info!("SD card task spawned");

    // Spawn LED output task (owns PIO/DMA, handles async output)
    info!("Spawning LED output task...");
    let led_pins = LedPins {
        pin0: p.PIN_0, pin1: p.PIN_1, pin2: p.PIN_2, pin3: p.PIN_3,
        pin4: p.PIN_4, pin5: p.PIN_5, pin6: p.PIN_6, pin7: p.PIN_7,
        pin8: p.PIN_8, pin9: p.PIN_9, pin10: p.PIN_10, pin11: p.PIN_11,
        pin12: p.PIN_12, pin13: p.PIN_13, pin14: p.PIN_14, pin15: p.PIN_15,
        pin16: p.PIN_16, pin17: p.PIN_17, pin18: p.PIN_18, pin19: p.PIN_19,
        pin20: p.PIN_20, pin21: p.PIN_21, pin22: p.PIN_22, pin23: p.PIN_23,
        pin24: p.PIN_24, pin25: p.PIN_25, pin26: p.PIN_26, pin27: p.PIN_27,
        pin28: p.PIN_28, pin29: p.PIN_29, pin30: p.PIN_30, pin31: p.PIN_31,
    };
    spawner.spawn(led_output_task(p.PIO1, p.DMA_CH2, led_pins).unwrap());
    info!("LED output task spawned");

    // Spawn LED runner task (handles swappable generators)
    info!("Spawning LED runner task...");
    spawner.spawn(led_runner_task().unwrap());
    info!("LED runner task spawned");

    // I2C1 on GPIO 46 (SDA) and GPIO 47 (SCL)
    info!("Initializing I2C...");
    let sda = p.PIN_46;
    let scl = p.PIN_47;
    let mut i2c_config = I2cConfig::default();
    i2c_config.frequency = 400_000;
    let i2c = I2c::new_async(p.I2C1, scl, sda, Irqs, i2c_config);
    info!("I2C initialized");

    // Initialize display
    info!("Initializing display...");
    let graphics_mode = Builder::new().connect_i2c(i2c).into();
    let mut display = Sh1106Display::new(graphics_mode);
    if let Err(e) = display.init() {
        info!("Display init failed: {:?}", defmt::Debug2Format(&e));
    } else {
        info!("Display initialized");
    }
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

            // Log when config loads
            use pixel_blit::state::BootState;
            if result.new.boot_state == BootState::Ready
                && result.old.boot_state != BootState::Ready
            {
                info!("Config loaded, string 0 color order: {:?}", defmt::Debug2Format(&result.new.strings[0].color_order));
            }

            // Trigger rescan when entering SD card menu
            let entered_sd = result.new.in_detail_view
                && result.new.menu_selection == MenuEntry::SdCard
                && !result.old.in_detail_view;
            if entered_sd {
                usb_println!("SD: menu entered, triggering rescan");
                SD_RESCAN_CHANNEL.try_send(result.new.board_id).ok();
            }

            // Swap generators based on run_state (single source of truth)
            let rainbow_running = result.new.rainbow_test.run_state == RunState::Running;
            let rainbow_was_running = result.old.rainbow_test.run_state == RunState::Running;
            let string_length_running = result.new.string_length.run_state == RunState::Running;
            let string_length_was_running = result.old.string_length.run_state == RunState::Running;

            if rainbow_running && !rainbow_was_running {
                // Rainbow just started
                info!("Rainbow started, string={}", result.new.rainbow_test.current_string);
                RAINBOW_GENERATOR.set_string(result.new.rainbow_test.current_string);
                led_driver::set_generator(Some(&RAINBOW_GENERATOR));
            } else if rainbow_running {
                // Rainbow still running - update string if changed
                if result.new.rainbow_test.current_string != result.old.rainbow_test.current_string {
                    RAINBOW_GENERATOR.set_string(result.new.rainbow_test.current_string);
                }
            } else if !rainbow_running && rainbow_was_running {
                // Rainbow just stopped
                info!("Rainbow stopped");
                led_driver::set_generator(None);
            }

            if string_length_running && !string_length_was_running {
                // String length just started
                STRING_LENGTH_GENERATOR.set_position(
                    result.new.string_length.current_string,
                    result.new.string_length.current_pixel,
                );
                led_driver::set_generator(Some(&STRING_LENGTH_GENERATOR));
            } else if string_length_running {
                // String length still running - update position if changed
                STRING_LENGTH_GENERATOR.set_position(
                    result.new.string_length.current_string,
                    result.new.string_length.current_pixel,
                );
            } else if !string_length_running && string_length_was_running {
                // String length just stopped
                led_driver::set_generator(None);
            }
        }
    }
}
