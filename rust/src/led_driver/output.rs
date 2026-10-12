//! LED output hardware driver.
//!
//! This module handles the PIO and DMA for LED output.
//! It receives frames from the runner via channels and outputs them via DMA.
//!
//! # WS2812 Timing
//!
//! WS2812/WS2811 LEDs use a single-wire protocol with timing-based encoding:
//!
//! | Symbol | Parameter       | Min   | Typ   | Max   |
//! |--------|-----------------|-------|-------|-------|
//! | T0H    | 0-bit high time | 250ns | 400ns | 550ns |
//! | T0L    | 0-bit low time  | 700ns | 850ns | 1000ns|
//! | T1H    | 1-bit high time | 650ns | 800ns | 950ns |
//! | T1L    | 1-bit low time  | 300ns | 450ns | 600ns |
//! | Treset | Reset pulse     | 50µs  | -     | -     |
//!
//! Total bit time: ~1.25µs (800kHz)
//!
//! # PIO Program Design
//!
//! The PIO program outputs 32 pins in parallel with the following approach:
//!
//! ```text
//!          T1 (3cy)      T2 (3cy)      T3 (4cy)
//!        ┌─────────┐   ┌─────────┐   ┌──────────┐
//!    1:  │  HIGH   │   │  HIGH   │   │   LOW    │  = 6 HIGH, 4 LOW
//!        ├─────────┤   ├─────────┤   ├──────────┤
//!    0:  │  HIGH   │   │   LOW   │   │   LOW    │  = 3 HIGH, 7 LOW
//!        └─────────┘   └─────────┘   └──────────┘
//! ```
//!
//! - T1: All pins HIGH (setup time, establishes pulse start)
//! - T2: Data-dependent (pins follow bit values)
//! - T3: All pins LOW (ensures proper low time)
//!
//! The clock divider is calculated at runtime from the system clock:
//! - Divider = clk_sys_freq() / (800kHz × 10 cycles) = clk_sys_freq() / 8MHz
//!
//! At 150MHz: divider = 18.75, giving exact 800kHz timing.

use embassy_rp::clocks::clk_sys_freq;
use embassy_rp::dma::Channel as DmaChannel;
use embassy_rp::pio::program::pio_asm;
use embassy_rp::pio::{
    Common, Config, Direction, Instance, Pin, PioPin, ShiftConfig, ShiftDirection, StateMachine,
};
use embassy_rp::Peri;
use embassy_time::Timer;
use fixed::types::U24F8;

use super::buffer::{FRAME_CHANNEL, FREE_CHANNEL};

/// Target WS2812 bit rate in Hz
const WS2812_BIT_RATE: u32 = 800_000;

/// PIO cycles per bit (T1 + T2 + T3 = 3 + 3 + 4 = 10)
const PIO_CYCLES_PER_BIT: u32 = 10;

/// Calculate PIO clock divider for WS2812 timing.
///
/// Returns a fixed-point divider that achieves 800kHz bit rate.
fn ws2812_clock_divider() -> U24F8 {
    let sys_freq = clk_sys_freq();
    let pio_freq = WS2812_BIT_RATE * PIO_CYCLES_PER_BIT; // 8MHz

    // Calculate divider as fixed-point: sys_freq / pio_freq
    // Use integer division with fractional part
    let int_part = sys_freq / pio_freq;
    let remainder = sys_freq % pio_freq;
    // Scale remainder to get 8 fractional bits (256 = 2^8)
    let frac_part = (remainder * 256) / pio_freq;

    // Combine into U24F8 format (integer part shifted left 8 bits + fractional)
    U24F8::from_bits((int_part << 8 | frac_part) as u32)
}

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
        // WS2812 parallel PIO program
        // See module docs for timing analysis.
        //
        // Cycle count per instruction (including delay):
        //   pull block:        1 cycle (blocks until data available)
        //   mov pins, !null:   1 + 2 delay = 3 cycles (T1 phase)
        //   out pins, 32:      1 + 2 delay = 3 cycles (T2 phase)
        //   mov pins, null:    1 + 3 delay = 4 cycles (T3 phase)
        // Total: 10 cycles per bit (+ 1 cycle pull, amortized over 32 bits)
        let prg = pio_asm!(
            ".wrap_target",
            "pull block",           // Wait for 32-bit word (pins stay LOW during wait)
            "mov pins, !null [2]",  // T1: All pins HIGH (3 cycles)
            "out pins, 32 [2]",     // T2: Output data bits to all 32 pins (3 cycles)
            "mov pins, null [3]",   // T3: All pins LOW (4 cycles)
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

        // Clock divider calculated from system clock for exact 800kHz timing
        cfg.clock_divider = ws2812_clock_divider();

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
    pub fn new_parallel_32(
        common: &mut Common<'d, PIO>,
        mut sm: StateMachine<'d, PIO, SM>,
    ) -> Self {
        // Configure GPIO 0-31 for PIO output via PAC
        configure_gpio_for_pio();

        // WS2812 parallel PIO program (same as LedOutput::new, see module docs)
        let prg = pio_asm!(
            ".wrap_target",
            "pull block",           // Wait for 32-bit word
            "mov pins, !null [2]",  // T1: All pins HIGH (3 cycles)
            "out pins, 32 [2]",     // T2: Output data bits (3 cycles)
            "mov pins, null [3]",   // T3: All pins LOW (4 cycles)
            ".wrap",
        );
        let loaded = common.load_program(&prg.program);

        // Configure state machine with direct PINCTRL settings
        let mut cfg = Config::default();
        cfg.use_program(&loaded, &[]);  // no side-set pins

        // Set OUT pins directly: base=0, count=32 via PAC
        set_pinctrl_out(&mut cfg);

        // Clock divider calculated from system clock for exact 800kHz timing
        cfg.clock_divider = ws2812_clock_divider();
        cfg.shift_out = ShiftConfig {
            auto_fill: false,
            threshold: 32,
            direction: ShiftDirection::Right,
        };
        cfg.fifo_join = embassy_rp::pio::FifoJoin::TxOnly;

        sm.set_config(&cfg);
        sm.set_enable(true);

        Self { sm }
    }
}

/// Configure GPIO 0-31 pads and function select for PIO1.
fn configure_gpio_for_pio() {
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

/// Set PINCTRL for 32 output pins starting at GPIO 0.
fn set_pinctrl_out<PIO: Instance>(cfg: &mut Config<'_, PIO>) {
    // Access pins config and set OUT base/count
    let mut pins = cfg.get_pins();
    pins.out_base = 0;
    pins.out_count = 32;
    // Safety: We've configured GPIO 0-31 for PIO1 in configure_gpio_for_pio()
    // before calling this function. The pins are dedicated to LED output.
    unsafe { cfg.set_pins(pins) };
}

/// Run the DMA output task.
///
/// This task:
/// 1. Waits for ReadToken from FRAME_CHANNEL
/// 2. Gets DMA slice directly from token (no copying!)
/// 3. Pushes data via DMA
/// 4. Adds WS2812 reset delay
/// 5. Returns WriteToken to FREE_CHANNEL
/// 6. Loops
pub async fn run_output_loop<'d, PIO: Instance, const SM: usize, DMA: DmaChannel>(
    mut output: LedOutput<'d, PIO, SM>,
    mut dma: Peri<'d, DMA>,
) -> ! {
    loop {
        // Wait for a frame
        let read_token = FRAME_CHANNEL.receive().await;
        let start = embassy_time::Instant::now();

        // Get DMA slice directly from token - no copying needed!
        let dma_data = read_token.as_dma_slice();
        let dma_len = read_token.dma_len();

        // Push via DMA
        if dma_len > 0 {
            output.dma_push(dma.reborrow(), dma_data).await;
            let dma_time = start.elapsed().as_micros();

            // WS2812 reset delay (200µs minimum, use 250 for margin)
            Timer::after_micros(250).await;
            let total_time = start.elapsed().as_micros();

            if total_time > 15000 {
                defmt::info!("OUTPUT SPIKE: total={}us dma={}us len={}", total_time, dma_time, dma_len);
            }
        }

        // Return the buffer
        let write_token = read_token.into_write_token();
        FREE_CHANNEL.send(write_token).await;
    }
}
