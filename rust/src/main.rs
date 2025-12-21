#![no_std]
#![no_main]

mod generators;
mod tasks;

use defmt::{info, debug};
use embassy_executor::Spawner;
use embassy_rp::gpio::{Input, Pull};
use embassy_rp::i2c::{self, Config as I2cConfig, I2c};
use embassy_rp::peripherals::{I2C1, PIO1};
use embassy_rp::pio::InterruptHandler as PioInterruptHandler;
use embassy_time::Timer;
use reducto::App;
use sh1106::Builder;
use {defmt_rtt as _, panic_probe as _};

use pixel_blit::action::Action;
use pixel_blit::display::Sh1106Display;
use pixel_blit::effect::AppEffect;
use pixel_blit::led_driver::{self, buffer};
use pixel_blit::reducer::AppReducer;
use pixel_blit::state::AppState;
use pixel_blit::storage::persist::Settings;
use pixel_blit::views::RootView;

use generators::{RAINBOW, STRING_LENGTH};
use tasks::{ACTION_CHANNEL, SD_RESCAN_CHANNEL, FSEQ_COMMAND_CHANNEL, FseqCommand};

embassy_rp::bind_interrupts!(pub struct Irqs {
    I2C1_IRQ => i2c::InterruptHandler<I2C1>;
    PIO1_IRQ_0 => PioInterruptHandler<PIO1>;
});

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_rp::init(Default::default());

    // Brief delay for probe to connect
    Timer::after_millis(100).await;

    info!("Pixel Blit Firmware starting...");

    // Load settings from flash
    let (mut settings, state) = Settings::init(p.FLASH, p.DMA_CH3, &spawner);
    info!("Settings: brightness={}, board={}", state.brightness_level, state.board_id);

    // Spawn button tasks
    let btn_next = Input::new(p.PIN_45, Pull::Up);
    let btn_select = Input::new(p.PIN_43, Pull::Up);
    spawner.spawn(tasks::button_next_task(btn_next).unwrap());
    spawner.spawn(tasks::button_select_task(btn_select).unwrap());

    // Spawn tick task
    spawner.spawn(tasks::tick_task().unwrap());

    // Spawn SD card task
    spawner.spawn(tasks::sd_card_task(
        p.SPI0, p.PIN_38, p.PIN_39, p.PIN_36, p.PIN_37,
        p.DMA_CH0, p.DMA_CH1, state.board_id,
    ).unwrap());

    // Initialize LED driver
    let led_token = buffer::init();

    // Consume GPIO 0-31 (used by PIO via PAC)
    drop((
        p.PIN_0, p.PIN_1, p.PIN_2, p.PIN_3, p.PIN_4, p.PIN_5, p.PIN_6, p.PIN_7,
        p.PIN_8, p.PIN_9, p.PIN_10, p.PIN_11, p.PIN_12, p.PIN_13, p.PIN_14, p.PIN_15,
        p.PIN_16, p.PIN_17, p.PIN_18, p.PIN_19, p.PIN_20, p.PIN_21, p.PIN_22, p.PIN_23,
        p.PIN_24, p.PIN_25, p.PIN_26, p.PIN_27, p.PIN_28, p.PIN_29, p.PIN_30, p.PIN_31,
    ));
    spawner.spawn(tasks::led_output_task(p.PIO1, p.DMA_CH2).unwrap());
    spawner.spawn(tasks::led_runner_task(led_token).unwrap());

    // Initialize display
    let mut i2c_config = I2cConfig::default();
    i2c_config.frequency = 400_000;
    let i2c = I2c::new_async(p.I2C1, p.PIN_47, p.PIN_46, Irqs, i2c_config);
    let graphics_mode = Builder::new().connect_i2c(i2c).into();
    let mut display = Sh1106Display::new(graphics_mode);
    display.init().ok();
    let view = RootView::new(display);

    // Create app
    let mut app: App<AppState, Action, AppReducer, _> = App::new(view, state);
    app.dispatch(Action::Boot);

    info!("Entering main loop");

    // Main loop
    loop {
        let action = ACTION_CHANNEL.receive().await;
        let effect = app.dispatch(action);

        match effect {
            AppEffect::Unchanged | AppEffect::None => {}

            AppEffect::Save => {
                let state = app.state();
                if settings.save_state(state) {
                    debug!("Saving: brightness={}, board={}",
                        state.brightness_level, state.board_id);
                }
            }

            AppEffect::RainbowStart => {
                let state = app.state();
                RAINBOW.set_string(state.rainbow_test.current_string);
                led_driver::set_generator(Some(&RAINBOW));
            }

            AppEffect::RainbowUpdate => {
                RAINBOW.set_string(app.state().rainbow_test.current_string);
            }

            AppEffect::RainbowStop => {
                led_driver::set_generator(None);
            }

            AppEffect::StringLengthStart => {
                let state = app.state();
                STRING_LENGTH.set_position(
                    state.string_length.current_string,
                    state.string_length.current_pixel,
                );
                led_driver::set_generator(Some(&STRING_LENGTH));
            }

            AppEffect::StringLengthUpdate => {
                let state = app.state();
                STRING_LENGTH.set_position(
                    state.string_length.current_string,
                    state.string_length.current_pixel,
                );
            }

            AppEffect::StringLengthStop => {
                led_driver::set_generator(None);
            }

            AppEffect::SdRescan => {
                SD_RESCAN_CHANNEL.try_send(app.state().board_id).ok();
            }

            AppEffect::FseqStart => {
                let state = app.state();
                FSEQ_COMMAND_CHANNEL.try_send(FseqCommand::Play {
                    file_index: state.sd_card.playing_index,
                    string_configs: state.strings,
                }).ok();
            }

            AppEffect::FseqStop => {
                FSEQ_COMMAND_CHANNEL.try_send(FseqCommand::Stop).ok();
            }
        }
    }
}
