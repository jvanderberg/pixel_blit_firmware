#![no_std]
#![no_main]

mod usb;

use core::sync::atomic::{AtomicU8, AtomicU32, Ordering};
use defmt::info;
use embassy_executor::Spawner;
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
use pixel_blit::effect::AppEffect;
use pixel_blit::led_driver::{self, buffer, Frame, FrameGenerator, WriteToken};
use pixel_blit::reducer::AppReducer;
use pixel_blit::sdcard::hw::SdReader;
use pixel_blit::sdcard::set_file_list;
use pixel_blit::state::AppState;
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
///
/// # Atomics Ordering
///
/// Uses `Relaxed` ordering because:
/// 1. Single atomic values (not compound data) - no consistency between fields needed
/// 2. Visual display tolerates brief inconsistencies (self-corrects next frame)
/// 3. Embassy's single-threaded executor provides implicit synchronization points
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
///
/// # Atomics Ordering
///
/// Uses a single `AtomicU32` to pack both string (bits 16-23) and pixel (bits 0-15)
/// for atomic updates. `Relaxed` ordering is sufficient since we only need
/// single-value atomicity, not cross-thread synchronization.
pub struct StringLengthGenerator {
    /// Packed position: bits 16-23 = string (u8), bits 0-15 = pixel (u16)
    position: AtomicU32,
}

impl StringLengthGenerator {
    pub const fn new() -> Self {
        // Default: string=0, pixel=1
        Self {
            position: AtomicU32::new(1),
        }
    }

    pub fn set_position(&self, string: u8, pixel: u16) {
        let packed = ((string as u32) << 16) | (pixel as u32);
        self.position.store(packed, Ordering::Relaxed);
    }
}

impl FrameGenerator for StringLengthGenerator {
    fn generate(&self, frame: &mut Frame) {
        let packed = self.position.load(Ordering::Relaxed);
        let string = (packed >> 16) as u8;
        let pixel = (packed & 0xFFFF) as u16;

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
// Timer Tick Task
// ============================================================================

#[embassy_executor::task]
async fn tick_task() {
    loop {
        Timer::after_secs(1).await;
        ACTION_CHANNEL.send(Action::Tick1s).await;
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
                // Brief delay before accepting next rescan to prevent rapid retries
                Timer::after_millis(500).await;
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


/// LED output task - owns PIO and DMA, handles buffer swapping and output.
///
/// Note: GPIO 0-31 are configured for PIO output via direct register access
/// in `LedOutput::new_parallel_32`. The pin Peri handles are consumed in main()
/// to prevent reuse, but not passed here.
#[embassy_executor::task]
async fn led_output_task(
    pio1: Peri<'static, PIO1>,
    dma: Peri<'static, embassy_rp::peripherals::DMA_CH2>,
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
    usb_println!("LED Output: running output loop");

    // Run the output loop forever
    led_driver::run_output_loop(output, dma).await
}

/// LED runner task - runs the generator loop.
/// Generators are swapped via led_driver::set_generator() from main loop.
#[embassy_executor::task]
async fn led_runner_task(token: WriteToken) {
    usb_println!("LED Runner: starting");
    Timer::after_millis(100).await;
    usb_println!("LED Runner: entering run loop");
    led_driver::run(token).await
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
    // Uses DMA_CH3 for flash operations
    let (mut settings, state) = Settings::init(p.FLASH, p.DMA_CH3, &spawner);
    usb_println!("Settings loaded: brightness={}", state.brightness_level);

    // Set up buttons (active low with pull-up) and spawn ISR tasks
    info!("Spawning button tasks...");
    let btn_next = Input::new(p.PIN_45, Pull::Up);
    let btn_select = Input::new(p.PIN_43, Pull::Up);
    match button_next_task(btn_next) {
        Ok(token) => spawner.spawn(token),
        Err(e) => defmt::error!("Failed to spawn button_next_task: {:?}", e),
    }
    match button_select_task(btn_select) {
        Ok(token) => spawner.spawn(token),
        Err(e) => defmt::error!("Failed to spawn button_select_task: {:?}", e),
    }
    info!("Button tasks spawned");

    // Spawn tick task for uptime tracking
    info!("Spawning tick task...");
    match tick_task() {
        Ok(token) => spawner.spawn(token),
        Err(e) => defmt::error!("Failed to spawn tick_task: {:?}", e),
    }
    info!("Tick task spawned");

    // SPI0 for SD card on GPIO 36-39
    // MISO=36, CS=37, SCK=38, MOSI=39
    info!("Spawning SD card task...");
    match sd_card_task(
        p.SPI0,
        p.PIN_38, // SCK
        p.PIN_39, // MOSI
        p.PIN_36, // MISO
        p.PIN_37, // CS
        p.DMA_CH0,
        p.DMA_CH1,
        state.board_id,
    ) {
        Ok(token) => spawner.spawn(token),
        Err(e) => defmt::error!("Failed to spawn sd_card_task: {:?}", e),
    }
    info!("SD card task spawned");

    // Initialize LED buffer system (returns first WriteToken)
    info!("Initializing LED buffer system...");
    let led_token = buffer::init();
    info!("LED buffer initialized");

    // Spawn LED output task (owns PIO/DMA, handles async output)
    // Note: GPIO 0-31 Peri handles must be consumed to prevent reuse.
    // The task configures them via PAC (direct register access).
    info!("Spawning LED output task...");
    drop((
        p.PIN_0, p.PIN_1, p.PIN_2, p.PIN_3, p.PIN_4, p.PIN_5, p.PIN_6, p.PIN_7,
        p.PIN_8, p.PIN_9, p.PIN_10, p.PIN_11, p.PIN_12, p.PIN_13, p.PIN_14, p.PIN_15,
        p.PIN_16, p.PIN_17, p.PIN_18, p.PIN_19, p.PIN_20, p.PIN_21, p.PIN_22, p.PIN_23,
        p.PIN_24, p.PIN_25, p.PIN_26, p.PIN_27, p.PIN_28, p.PIN_29, p.PIN_30, p.PIN_31,
    ));
    match led_output_task(p.PIO1, p.DMA_CH2) {
        Ok(token) => spawner.spawn(token),
        Err(e) => defmt::error!("Failed to spawn led_output_task: {:?}", e),
    }
    info!("LED output task spawned");

    // Spawn LED runner task (handles swappable generators)
    info!("Spawning LED runner task...");
    match led_runner_task(led_token) {
        Ok(token) => spawner.spawn(token),
        Err(e) => defmt::error!("Failed to spawn led_runner_task: {:?}", e),
    }
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

    // Main loop - dispatch actions and handle effects
    loop {
        let action = ACTION_CHANNEL.receive().await;
        let effect = app.dispatch(action);

        // Handle effects - the reducer explicitly signals what side effects are needed
        match effect {
            AppEffect::Unchanged => {
                // No state change, nothing to do
            }

            AppEffect::None => {
                // State changed, view already rendered by App::dispatch
            }

            AppEffect::Save => {
                // Save settings to flash
                let state = app.state();
                if settings.save_state(state) {
                    usb_println!("Saving: brightness={}, board={}",
                        state.brightness_level, state.board_id);
                }
            }

            AppEffect::RainbowStart => {
                let state = app.state();
                info!("Rainbow started, string={}", state.rainbow_test.current_string);
                RAINBOW_GENERATOR.set_string(state.rainbow_test.current_string);
                led_driver::set_generator(Some(&RAINBOW_GENERATOR));
            }

            AppEffect::RainbowUpdate => {
                let state = app.state();
                RAINBOW_GENERATOR.set_string(state.rainbow_test.current_string);
            }

            AppEffect::RainbowStop => {
                info!("Rainbow stopped");
                led_driver::set_generator(None);
            }

            AppEffect::StringLengthStart => {
                let state = app.state();
                STRING_LENGTH_GENERATOR.set_position(
                    state.string_length.current_string,
                    state.string_length.current_pixel,
                );
                led_driver::set_generator(Some(&STRING_LENGTH_GENERATOR));
            }

            AppEffect::StringLengthUpdate => {
                let state = app.state();
                STRING_LENGTH_GENERATOR.set_position(
                    state.string_length.current_string,
                    state.string_length.current_pixel,
                );
            }

            AppEffect::StringLengthStop => {
                led_driver::set_generator(None);
            }

            AppEffect::SdRescan => {
                let state = app.state();
                usb_println!("SD: triggering rescan for board {}", state.board_id);
                SD_RESCAN_CHANNEL.try_send(state.board_id).ok();
            }
        }
    }
}
