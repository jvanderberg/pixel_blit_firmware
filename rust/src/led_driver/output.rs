//! LED output hardware driver.
//!
//! This module handles the PIO and DMA for LED output.
//! It's used internally by the runner module.

use embassy_rp::dma::Channel as DmaChannel;
use embassy_rp::pio::program::pio_asm;
use embassy_rp::pio::{
    Common, Config, Direction, Instance, Pin, PioPin, ShiftConfig, ShiftDirection, StateMachine,
};
use embassy_rp::Peri;
use fixed::traits::ToFixed;
use fixed_macro::types::U56F8;

use super::runner;

// Clock divider calculation:
// WS2812 bit rate: 800kHz, PIO cycles per bit: T1(3) + T2(3) + T3(4) + pull(1) = 11
// Target PIO clock: 800_000 * 11 = 8.8MHz
// RP2350 sys clock: 150MHz, divider = 150/8.8 = 17.045

/// Low-level LED output driver.
///
/// Owns PIO state machine. Used by the DMA task.
pub struct LedOutput<'d, PIO: Instance, const SM: usize> {
    sm: StateMachine<'d, PIO, SM>,
}

impl<'d, PIO: Instance, const SM: usize> LedOutput<'d, PIO, SM> {
    /// Create a new LED output driver.
    pub fn new<P0, P1, P2, P3, P4, P5, P6, P7, P8, P9, P10, P11, P12, P13, P14, P15, P16, P17, P18, P19, P20, P21, P22, P23, P24, P25, P26, P27, P28, P29, P30, P31>(
        common: &mut Common<'d, PIO>,
        mut sm: StateMachine<'d, PIO, SM>,
        pin0: Peri<'d, P0>,
        pin1: Peri<'d, P1>,
        pin2: Peri<'d, P2>,
        pin3: Peri<'d, P3>,
        pin4: Peri<'d, P4>,
        pin5: Peri<'d, P5>,
        pin6: Peri<'d, P6>,
        pin7: Peri<'d, P7>,
        pin8: Peri<'d, P8>,
        pin9: Peri<'d, P9>,
        pin10: Peri<'d, P10>,
        pin11: Peri<'d, P11>,
        pin12: Peri<'d, P12>,
        pin13: Peri<'d, P13>,
        pin14: Peri<'d, P14>,
        pin15: Peri<'d, P15>,
        pin16: Peri<'d, P16>,
        pin17: Peri<'d, P17>,
        pin18: Peri<'d, P18>,
        pin19: Peri<'d, P19>,
        pin20: Peri<'d, P20>,
        pin21: Peri<'d, P21>,
        pin22: Peri<'d, P22>,
        pin23: Peri<'d, P23>,
        pin24: Peri<'d, P24>,
        pin25: Peri<'d, P25>,
        pin26: Peri<'d, P26>,
        pin27: Peri<'d, P27>,
        pin28: Peri<'d, P28>,
        pin29: Peri<'d, P29>,
        pin30: Peri<'d, P30>,
        pin31: Peri<'d, P31>,
    ) -> Self
    where
        P0: PioPin, P1: PioPin, P2: PioPin, P3: PioPin,
        P4: PioPin, P5: PioPin, P6: PioPin, P7: PioPin,
        P8: PioPin, P9: PioPin, P10: PioPin, P11: PioPin,
        P12: PioPin, P13: PioPin, P14: PioPin, P15: PioPin,
        P16: PioPin, P17: PioPin, P18: PioPin, P19: PioPin,
        P20: PioPin, P21: PioPin, P22: PioPin, P23: PioPin,
        P24: PioPin, P25: PioPin, P26: PioPin, P27: PioPin,
        P28: PioPin, P29: PioPin, P30: PioPin, P31: PioPin,
    {
        // WS2811 parallel PIO program
        // T1=3, T2=3, T3=4 cycles
        let prg = pio_asm!(
            ".wrap_target",
            "pull block",           // Wait for data (pins stay LOW)
            "mov pins, !null [2]",  // All pins HIGH for T1 (3 cycles)
            "out pins, 32 [2]",     // Output data bits for T2 (3 cycles)
            "mov pins, null [3]",   // All pins LOW for T3 (4 cycles)
            ".wrap",
        );

        let loaded = common.load_program(&prg.program);

        // Make PIO pins
        let pins: [Pin<'d, PIO>; 32] = [
            common.make_pio_pin(pin0),
            common.make_pio_pin(pin1),
            common.make_pio_pin(pin2),
            common.make_pio_pin(pin3),
            common.make_pio_pin(pin4),
            common.make_pio_pin(pin5),
            common.make_pio_pin(pin6),
            common.make_pio_pin(pin7),
            common.make_pio_pin(pin8),
            common.make_pio_pin(pin9),
            common.make_pio_pin(pin10),
            common.make_pio_pin(pin11),
            common.make_pio_pin(pin12),
            common.make_pio_pin(pin13),
            common.make_pio_pin(pin14),
            common.make_pio_pin(pin15),
            common.make_pio_pin(pin16),
            common.make_pio_pin(pin17),
            common.make_pio_pin(pin18),
            common.make_pio_pin(pin19),
            common.make_pio_pin(pin20),
            common.make_pio_pin(pin21),
            common.make_pio_pin(pin22),
            common.make_pio_pin(pin23),
            common.make_pio_pin(pin24),
            common.make_pio_pin(pin25),
            common.make_pio_pin(pin26),
            common.make_pio_pin(pin27),
            common.make_pio_pin(pin28),
            common.make_pio_pin(pin29),
            common.make_pio_pin(pin30),
            common.make_pio_pin(pin31),
        ];

        // Set all pins as outputs, initially LOW
        let pin_refs: [&Pin<'d, PIO>; 32] = core::array::from_fn(|i| &pins[i]);
        sm.set_pin_dirs(Direction::Out, &pin_refs);

        // Configure state machine
        let mut cfg = Config::default();
        cfg.use_program(&loaded, &pin_refs);

        // Clock divider for 800kHz bit rate
        cfg.clock_divider = U56F8!(17.045).to_fixed();

        // Shift out right, auto-pull disabled (we use explicit pull)
        cfg.shift_out = ShiftConfig {
            auto_fill: false,
            threshold: 32,
            direction: ShiftDirection::Right,
        };

        // FIFO join for TX only (8 words deep)
        cfg.fifo_join = embassy_rp::pio::FifoJoin::TxOnly;

        sm.set_config(&cfg);
        sm.set_enable(true);

        Self { sm }
    }

    /// Push data to PIO via DMA.
    pub async fn dma_push<DMA: DmaChannel>(&mut self, dma: Peri<'_, DMA>, data: &[u32]) {
        let (_, tx) = self.sm.rx_tx();
        tx.dma_push(dma, data, false).await;
    }

    /// Create LED output for GPIO 0-31 using direct register access.
    ///
    /// This bypasses embassy's Pin abstraction to avoid stack overflow from 32 Pin structs.
    ///
    /// # Safety
    /// Caller must ensure GPIO 0-31 are not used elsewhere.
    pub fn new_parallel_32(
        common: &mut Common<'d, PIO>,
        mut sm: StateMachine<'d, PIO, SM>,
    ) -> Self {
        // Configure GPIO 0-31 for PIO output
        // Safety: We're taking exclusive control of these pins for LED output
        unsafe {
            Self::configure_gpio_for_pio();
        }

        // Load PIO program
        let prg = pio_asm!(
            ".wrap_target",
            "pull block",
            "mov pins, !null [2]",
            "out pins, 32 [2]",
            "mov pins, null [3]",
            ".wrap",
        );
        let loaded = common.load_program(&prg.program);

        // Configure state machine with direct PINCTRL settings
        let mut cfg = Config::default();
        cfg.use_program(&loaded, &[]);  // no side-set pins

        // Set OUT pins directly: base=0, count=32
        // Safety: We've configured GPIO 0-31 for PIO above
        unsafe {
            let mut pins = cfg.get_pins();
            pins.out_base = 0;
            pins.out_count = 32;
            cfg.set_pins(pins);
        }

        cfg.clock_divider = U56F8!(17.045).to_fixed();
        cfg.shift_out = ShiftConfig {
            auto_fill: false,
            threshold: 32,
            direction: ShiftDirection::Right,
        };
        cfg.fifo_join = embassy_rp::pio::FifoJoin::TxOnly;

        sm.set_config(&cfg);

        // Set pin directions to output via PIO
        // Safety: Direct register access for pin directions
        unsafe {
            Self::set_pin_dirs_out(&sm);
        }

        sm.set_enable(true);

        Self { sm }
    }

    /// Configure GPIO 0-31 pads and function select for PIO1.
    unsafe fn configure_gpio_for_pio() {
        use embassy_rp::pac;
        use embassy_rp::pac::io::vals::Oeover;

        // PIO1 function select = 7 on RP2350
        const PIO1_FUNCSEL: u8 = 7;

        for pin in 0u8..32 {
            let gpio = pac::IO_BANK0.gpio(pin as usize);
            let pads = pac::PADS_BANK0.gpio(pin as usize);

            // Set function select to PIO1, and force OE high
            // OEOVER::ENABLE forces output enable, overriding PIO's OE signal
            gpio.ctrl().write(|w| {
                w.set_funcsel(PIO1_FUNCSEL);
                w.set_oeover(Oeover::ENABLE);
            });

            // Configure pad: output enabled, schmitt trigger, normal slew
            pads.write(|w| {
                w.set_od(false);      // Output enable (not disabled)
                w.set_ie(true);       // Input enable (for reading back)
                w.set_schmitt(true);  // Schmitt trigger
                w.set_slewfast(false); // Normal slew rate
                w.set_pue(false);     // No pull-up
                w.set_pde(false);     // No pull-down
            });
        }
    }

    /// Set GPIO 0-31 as outputs via PIO pin direction register.
    unsafe fn set_pin_dirs_out<const SM_IDX: usize>(_sm: &StateMachine<'d, PIO, SM_IDX>) {
        // Pin directions are now forced via OEOVER in configure_gpio_for_pio()
        // No additional action needed here
    }
}

/// Run the DMA output task.
///
/// This task:
/// 1. Waits for FRAME_READY signal
/// 2. Swaps buffers
/// 3. Signals BUFFER_FREE (writer can start next frame)
/// 4. Pushes data via DMA (awaits completion)
/// 5. Loops
///
/// Call this from an embassy task.
pub async fn run_output_loop<'d, PIO: Instance, const SM: usize, DMA: DmaChannel>(
    mut output: LedOutput<'d, PIO, SM>,
    mut dma: Peri<'d, DMA>,
) -> ! {
    // Signal initial buffer free
    runner::signal_buffer_free();

    let mut frame_count: u32 = 0;

    loop {
        // Wait for writer to signal frame ready
        defmt::trace!("Output: waiting for frame ready");
        runner::wait_frame_ready().await;
        defmt::trace!("Output: frame ready received");

        // Safety: We have exclusive access to buffer after FRAME_READY
        let buffer = unsafe { runner::get_buffer() };

        // Swap buffers - back becomes front for DMA
        buffer.swap();

        // Signal writer that back buffer is free
        // Writer can now start preparing next frame
        runner::signal_buffer_free();

        // Get DMA data from front buffer
        let data = buffer.as_dma_words();

        frame_count = frame_count.wrapping_add(1);
        if frame_count % 600 == 0 {
            defmt::info!("Output: DMA frame {}, {} words", frame_count, data.len());
        }

        // Push via DMA - this awaits but doesn't block writer
        output.dma_push(dma.reborrow(), data).await;
    }
}
