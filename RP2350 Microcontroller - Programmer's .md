# RP2350 Microcontroller - Programmer's Reference for C/C++ Development

## Overview

The RP2350 is Raspberry Pi's second-generation microcontroller, offering significant improvements over the RP2040. It features dual-architecture support (Arm Cortex-M33 or RISC-V Hazard3), enhanced security, more memory, and backward compatibility with RP2040 code.

### Key Specifications

- **Processors**: Dual-core Arm Cortex-M33 OR dual-core RISC-V Hazard3 (selectable, not simultaneous)
- **Clock Speed**: Up to 150 MHz
- **Memory**: 520 KB on-chip SRAM (10 banks of 52KB each)
- **Flash Support**: External QSPI flash up to 16MB (via XIP - Execute In Place)
- **GPIO**: 30 pins (QFN-60) or 48 pins (QFN-80)
- **ADC**: 12-bit SAR ADC with 4 or 8 channels (depending on package)
- **Security**: Secure boot, ARM TrustZone, OTP memory, glitch detector
- **Peripherals**: 2x UART, 2x SPI, 2x I2C, 16x PWM, 12x DMA, 2x PIO, USB 1.1

### Chip Variants

- **RP2350A**: QFN-60, 30 GPIO, no internal flash
- **RP2350B**: QFN-80, 48 GPIO, no internal flash
- **RP2354A**: QFN-60, 30 GPIO, 2MB internal flash
- **RP2354B**: QFN-80, 48 GPIO, 2MB internal flash

## Processor Architecture

### Dual Architecture Design

The RP2350 can run either Arm or RISC-V cores, but not simultaneously. The architecture is selected at boot time.

**Arm Cortex-M33 Features:**
- ARMv8-M architecture with Thumb-2 instruction set
- Hardware FPU (single-precision floating point)
- DSP instructions
- ARM TrustZone security extensions
- 4KB instruction cache per core
- Memory Protection Unit (MPU)

**RISC-V Hazard3 Features:**
- RV32IMAC instruction set (32-bit integer, multiply/divide, atomic, compressed)
- Custom extensions for fast interrupts and GPIO
- Memory Protection Unit
- 4KB instruction cache per core

### Coprocessors (Cortex-M33 only)

1. **GPIO Coprocessor (GPIOC)**: Fast GPIO operations via coprocessor instructions
2. **Double-Precision Coprocessor (DCP)**: Simplified double-precision FP operations (add, sub, mul, div, sqrt)
3. **Redundancy Coprocessor (RCP)**: Fault detection for security-critical applications

### CPU Performance

- **Clock frequency**: Default 150 MHz, can run slower for power savings
- **Instruction timing**: Most instructions execute in 1 cycle
- **Interrupt latency**: 12 cycles (Arm), faster with tail-chaining

## Memory Architecture

### SRAM Organization

```
Address Range           Size    Description
0x20000000-0x2007FFFF  520KB   Main SRAM (10 banks × 52KB)
```

**Important SRAM Details:**
- 10 independent banks can be powered down individually for power savings
- Each bank is 52KB (0xD000 bytes)
- All SRAM is accessible as one contiguous block
- Zero-wait-state access at system clock speeds
- Supports 8-bit, 16-bit, and 32-bit accesses

**SRAM Striping:** The 10 banks are interleaved at 4KB boundaries to improve performance with sequential access patterns.

### ROM

```
Address Range           Size    Description
0x00000000-0x00003FFF  16KB    Bootrom
```

The bootrom contains:
- Boot sequence code
- USB/UART bootloader
- API functions for flash programming, cryptography, etc.
- Startup code

**Accessing ROM Functions:** Use function pointers found via the ROM table lookup mechanism (see Bootrom API section).

### Flash (XIP)

```
Address Range           Size        Description
0x10000000-0x10FFFFFF  16MB max    External QSPI flash
```

**XIP (Execute In Place) System:**
- Allows direct execution from external flash
- 16KB cache per core for performance
- Supports QSPI flash memories
- Can also support QSPI PSRAM
- Standard flash: W25Q series from Winbond, similar chips

**Cache Details:**
- Each core has independent 16KB instruction cache
- Cache line size: 8 bytes
- Cache provides near-SRAM performance for most code

### OTP (One-Time Programmable)

```
Address Range           Size    Description
0x00104000-0x00105FFF  8KB     OTP memory (read via data register)
```

OTP stores:
- Boot configuration
- Security keys
- USB vendor/product IDs
- Chip-unique ID
- User data

**Warning:** OTP can only be written once per bit. Use SDK functions for OTP access.

### Memory Map Summary

```
Address           Size      Region
0x00000000       16KB      ROM (bootrom)
0x10000000       16MB      XIP (flash/PSRAM)
0x20000000       520KB     SRAM
0x40000000       -         APB peripherals
0x50000000       -         AHB peripherals
0xD0000000       -         Core-local peripherals (SIO)
0xE0000000       -         Cortex-M33 private peripherals
```

## GPIO and Pin Control

### GPIO Architecture

The RP2350 has two GPIO banks:
- **Bank 0 (User Bank)**: 30 or 48 GPIOs (depending on package)
- **Bank 1 (QSPI Bank)**: 6 GPIOs dedicated to QSPI flash interface

Each GPIO can be configured for:
- Digital I/O (via SIO)
- Peripheral functions (UART, SPI, I2C, PWM, PIO)
- Interrupts (edge or level triggered)

### GPIO Function Selection

Each GPIO pin has multiple functions selected via the `GPIO_CTRL` register:

**Function Values (Bank 0):**
- `0`: SPI
- `1`: UART
- `2`: I2C
- `3`: PWM
- `4`: SIO (software-controlled GPIO)
- `5`: PIO0
- `6`: PIO1
- `7`: PIO2
- `8`: USB (specific pins only)
- `9`: UART (alternate)
- `11`: HSTX (high-speed transmit)
- `31`: NULL (no function)

### Basic GPIO Operations (C/C++)

```c
// Include SDK headers
#include "hardware/gpio.h"

// Initialize a GPIO pin as output
gpio_init(PIN_NUMBER);
gpio_set_dir(PIN_NUMBER, GPIO_OUT);

// Set pin high/low
gpio_put(PIN_NUMBER, 1);  // High
gpio_put(PIN_NUMBER, 0);  // Low

// Initialize as input
gpio_init(PIN_NUMBER);
gpio_set_dir(PIN_NUMBER, GPIO_IN);

// Read pin state
bool state = gpio_get(PIN_NUMBER);

// Enable pull-up/pull-down
gpio_pull_up(PIN_NUMBER);
gpio_pull_down(PIN_NUMBER);
gpio_disable_pulls(PIN_NUMBER);

// Set function
gpio_set_function(PIN_NUMBER, GPIO_FUNC_UART);
```

### GPIO Registers (Direct Access)

```c
// SIO GPIO registers (core-local, fast access)
#define SIO_BASE 0xd0000000
#define GPIO_OUT      (*(volatile uint32_t*)(SIO_BASE + 0x010))
#define GPIO_OUT_SET  (*(volatile uint32_t*)(SIO_BASE + 0x014))
#define GPIO_OUT_CLR  (*(volatile uint32_t*)(SIO_BASE + 0x018))
#define GPIO_OUT_XOR  (*(volatile uint32_t*)(SIO_BASE + 0x01c))
#define GPIO_IN       (*(volatile uint32_t*)(SIO_BASE + 0x004))

// Set GPIO 15 high (fast)
GPIO_OUT_SET = (1u << 15);

// Clear GPIO 15 (fast)
GPIO_OUT_CLR = (1u << 15);

// Toggle GPIO 15 (fast)
GPIO_OUT_XOR = (1u << 15);
```

### Pad Control

Each GPIO has pad control registers for electrical characteristics:

```c
#include "hardware/gpio.h"

// Set drive strength (2mA, 4mA, 8mA, 12mA)
gpio_set_drive_strength(PIN_NUMBER, GPIO_DRIVE_STRENGTH_12MA);

// Enable Schmitt trigger
gpio_set_schmitt(PIN_NUMBER, true);

// Set slew rate (slow/fast)
gpio_set_slew_rate(PIN_NUMBER, GPIO_SLEW_RATE_FAST);
```

### GPIO Interrupts

Each GPIO can generate interrupts on:
- Level high
- Level low
- Edge high (rising edge)
- Edge low (falling edge)

```c
#include "hardware/gpio.h"

// Callback function
void gpio_callback(uint gpio, uint32_t events) {
    // Handle interrupt
    if (events & GPIO_IRQ_EDGE_RISE) {
        // Rising edge detected
    }
}

// Setup interrupt
gpio_set_irq_enabled_with_callback(PIN_NUMBER, 
    GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, 
    true, 
    &gpio_callback);
```

**Important:** There are 4 independent GPIO interrupt controllers, allowing different cores or priority levels to handle different sets of GPIOs.

## Peripheral Interfaces

### UART

Two UART peripherals (UART0, UART1) based on ARM PL011.

**Features:**
- Programmable baud rate
- 5-8 data bits
- 1-2 stop bits
- Optional parity
- Hardware flow control (RTS/CTS)
- 32-byte TX and RX FIFOs
- DMA support

**Default GPIO Pins:**
```
UART0: TX=GPIO0,  RX=GPIO1,  CTS=GPIO2,  RTS=GPIO3
UART1: TX=GPIO4,  RX=GPIO5,  CTS=GPIO6,  RTS=GPIO7
Alternate: TX=GPIO8, RX=GPIO9, CTS=GPIO10, RTS=GPIO11
```

**Basic UART Usage:**

```c
#include "hardware/uart.h"

// Initialize UART
uart_init(uart0, 115200);
gpio_set_function(0, GPIO_FUNC_UART);
gpio_set_function(1, GPIO_FUNC_UART);

// Send data
uart_putc(uart0, 'A');
uart_puts(uart0, "Hello\n");

// Receive data
char c = uart_getc(uart0);

// Check if data available
if (uart_is_readable(uart0)) {
    char c = uart_getc(uart0);
}

// Blocking write
uart_write_blocking(uart0, data_buffer, length);

// Non-blocking read
uart_read_blocking(uart0, data_buffer, length);
```

**Baud Rate Calculation:**
```
UART_CLK = peripheral_clock (usually same as system clock)
Baud_Divider = (UART_CLK / (16 × baud_rate))
```

### SPI

Two SPI peripherals (SPI0, SPI1) based on ARM PL022.

**Features:**
- Master or slave mode
- Programmable bit rate up to 62.5 Mbps
- 4-16 bits per word
- Configurable clock polarity and phase
- 8-level TX and RX FIFOs
- DMA support

**Default GPIO Pins:**
```
SPI0: RX=GPIO0,  CSn=GPIO1,  SCK=GPIO2,  TX=GPIO3
      RX=GPIO4,  CSn=GPIO5,  SCK=GPIO6,  TX=GPIO7
SPI1: RX=GPIO8,  CSn=GPIO9,  SCK=GPIO10, TX=GPIO11
      RX=GPIO12, CSn=GPIO13, SCK=GPIO14, TX=GPIO15
```

**Basic SPI Usage:**

```c
#include "hardware/spi.h"

// Initialize SPI at 1 MHz
spi_init(spi0, 1000000);
gpio_set_function(2, GPIO_FUNC_SPI);  // SCK
gpio_set_function(3, GPIO_FUNC_SPI);  // TX
gpio_set_function(0, GPIO_FUNC_SPI);  // RX
gpio_set_function(1, GPIO_FUNC_SPI);  // CSn

// Set format: 8 bits, CPOL=0, CPHA=0
spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

// Write/read
uint8_t data_out = 0x42;
uint8_t data_in;
spi_write_read_blocking(spi0, &data_out, &data_in, 1);

// Write only
spi_write_blocking(spi0, data_buffer, length);

// Read only
spi_read_blocking(spi0, 0, data_buffer, length);
```

### I2C

Two I2C peripherals (I2C0, I2C1) based on Synopsys DesignWare.

**Features:**
- Master or slave mode
- Standard (100 kHz) and Fast mode (400 kHz) operation
- Fast mode plus (1 MHz) support
- 7-bit and 10-bit addressing
- Clock stretching
- Multi-master arbitration
- DMA support

**Default GPIO Pins:**
```
I2C0: SDA=GPIO0,  SCL=GPIO1
      SDA=GPIO4,  SCL=GPIO5
I2C1: SDA=GPIO2,  SCL=GPIO3
      SDA=GPIO6,  SCL=GPIO7
```

**Basic I2C Usage:**

```c
#include "hardware/i2c.h"

// Initialize I2C at 400 kHz
i2c_init(i2c0, 400000);
gpio_set_function(4, GPIO_FUNC_I2C);
gpio_set_function(5, GPIO_FUNC_I2C);
gpio_pull_up(4);  // Enable pull-ups
gpio_pull_up(5);

// Write to I2C device
uint8_t data[] = {0x00, 0x01, 0x02};
i2c_write_blocking(i2c0, DEVICE_ADDR, data, sizeof(data), false);

// Read from I2C device
uint8_t buffer[16];
i2c_read_blocking(i2c0, DEVICE_ADDR, buffer, sizeof(buffer), false);

// Write then read (common pattern for register read)
uint8_t reg = 0x10;
i2c_write_blocking(i2c0, DEVICE_ADDR, &reg, 1, true);  // Keep control
i2c_read_blocking(i2c0, DEVICE_ADDR, buffer, 1, false);

// Timeout versions available
i2c_write_timeout_us(i2c0, DEVICE_ADDR, data, len, false, timeout_us);
```

### PWM

16 PWM slices, each with two output channels (32 PWM outputs total).

**Features:**
- Dual channel per slice (A and B outputs)
- 16-bit counter with 8.4 fractional clock divider
- Multiple modes: free-running, level/edge triggered, gated
- Phase-correct and trailing-edge modes
- Interrupt on wrap

**GPIO to PWM Mapping:**
```c
// PWM channel = (GPIO_NUM / 2)
// PWM output  = (GPIO_NUM % 2) ? B : A
// GPIO 0  -> PWM0 A
// GPIO 1  -> PWM0 B
// GPIO 2  -> PWM1 A
// GPIO 3  -> PWM1 B
// ...
// GPIO 28 -> PWM6 A
// GPIO 29 -> PWM6 B
```

**Basic PWM Usage:**

```c
#include "hardware/pwm.h"

// Setup PWM on GPIO 15
gpio_set_function(15, GPIO_FUNC_PWM);
uint slice_num = pwm_gpio_to_slice_num(15);

// Set PWM frequency (wrap value)
pwm_set_wrap(slice_num, 65535);  // 16-bit resolution

// Set duty cycle (0-65535 for 0-100%)
pwm_set_gpio_level(15, 32768);  // 50% duty cycle

// Enable PWM
pwm_set_enabled(slice_num, true);

// Clock divider for frequency control
// PWM_freq = sys_clk / (div * wrap)
pwm_set_clkdiv(slice_num, 4.0f);
```

### ADC

12-bit SAR ADC with 500 ksps.

**Features:**
- 12-bit resolution
- 4 external channels (GPIO26-29 on QFN-60) or 8 channels (QFN-80)
- 1 internal temperature sensor channel
- Round-robin sampling mode
- FIFO for multiple samples
- DMA support

**Basic ADC Usage:**

```c
#include "hardware/adc.h"

// Initialize ADC
adc_init();

// Initialize GPIO for ADC (GPIO26-29)
adc_gpio_init(26);

// Select ADC input 0 (GPIO26)
adc_select_input(0);

// Read single sample (0-4095)
uint16_t result = adc_read();

// Read with temperature sensor
adc_select_input(4);  // Input 4 is temperature sensor
uint16_t temp_raw = adc_read();

// Convert to temperature (approximate)
const float conversion = 3.3f / 4096.0f;
float voltage = temp_raw * conversion;
float temp_c = 27.0f - (voltage - 0.706f) / 0.001721f;
```

**ADC FIFO Mode:**

```c
// Setup FIFO
adc_fifo_setup(
    true,    // Enable FIFO
    false,   // Disable DMA
    1,       // FIFO threshold
    false,   // No error bit
    false    // No byte shift
);

// Start free-running mode
adc_set_round_robin(0x01);  // Channel 0 only
adc_run(true);

// Read from FIFO
while (!adc_fifo_is_empty()) {
    uint16_t sample = adc_fifo_get();
}
```

### USB

USB 1.1 device controller with integrated PHY.

**Features:**
- Full-speed (12 Mbps) device only
- 16 endpoints (8 IN, 8 OUT)
- Integrated USB PHY
- DMA support
- Built-in bootloader with USB mass storage

**SDK Support:**
The Pico SDK uses TinyUSB for USB functionality. Common use cases:

```c
// USB CDC (Serial) - uses TinyUSB
#include "tusb.h"

// Check if USB serial is connected
if (tud_cdc_connected()) {
    // Send data
    tud_cdc_write_str("Hello USB\n");
    tud_cdc_write_flush();
}

// USB task (call regularly in main loop)
tud_task();
```

**USB GPIOs:**
- GPIO bootloader uses USB by default
- No GPIO configuration needed for USB device mode
- USB DP/DM pins are dedicated

### DMA

12 independent DMA channels with flexible configuration.

**Features:**
- 12 independent channels
- Memory-to-memory, memory-to-peripheral, peripheral-to-memory
- Configurable transfer sizes (8/16/32-bit)
- Ring buffer support
- Chain transfers
- Data request (DREQ) pacing from peripherals

**Basic DMA Usage:**

```c
#include "hardware/dma.h"

// Claim a DMA channel
int chan = dma_claim_unused_channel(true);

// Configure channel
dma_channel_config c = dma_channel_get_default_config(chan);
channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
channel_config_set_read_increment(&c, true);
channel_config_set_write_increment(&c, true);

// Start transfer
dma_channel_configure(
    chan,
    &c,
    dest,           // Destination
    src,            // Source
    count,          // Transfer count
    true            // Start immediately
);

// Wait for completion
dma_channel_wait_for_finish_blocking(chan);
```

**DMA with Peripherals:**

```c
// DMA from memory to SPI
dma_channel_config c = dma_channel_get_default_config(chan);
channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
channel_config_set_dreq(&c, DREQ_SPI0_TX);  // Pace to SPI

dma_channel_configure(chan, &c, 
    &spi_get_hw(spi0)->dr,  // Write to SPI data register
    buffer,                  // Read from buffer
    buffer_len,
    true);
```

## PIO (Programmable I/O)

Three PIO blocks (PIO0, PIO1, PIO2), each with 4 state machines.

**Features per PIO block:**
- 4 state machines
- 32 instructions of program memory (shared)
- 4×32-bit TX FIFOs, 4×32-bit RX FIFOs
- Flexible GPIO mapping
- Clock dividers per state machine
- DMA support

**PIO is incredibly powerful for:**
- Custom protocols (I2S, WS2812, DVI, VGA)
- High-speed parallel interfaces
- Unusual timing requirements
- Offloading CPU from bit-banging

**Basic PIO Usage:**

```c
#include "hardware/pio.h"
#include "my_program.pio.h"  // Generated by pioasm

// Load program
uint offset = pio_add_program(pio0, &my_program_program);

// Get state machine
uint sm = pio_claim_unused_sm(pio0, true);

// Initialize state machine
my_program_program_init(pio0, sm, offset, pin);

// Write data to FIFO
pio_sm_put_blocking(pio0, sm, data);

// Read data from FIFO
uint32_t data = pio_sm_get_blocking(pio0, sm);
```

**PIO Programs:**

PIO programs are written in PIO assembly language and assembled with `pioasm`:

```asm
.program my_program

.wrap_target
    out pins, 1
    nop [31]
.wrap
```

Compile with:
```bash
pioasm my_program.pio my_program.pio.h
```

## Timers and Timing

### System Timer

64-bit microsecond timer running at 1 MHz.

```c
#include "hardware/timer.h"

// Get current time in microseconds
uint64_t now = time_us_64();

// Delay
sleep_us(1000);    // 1ms delay
sleep_ms(1000);    // 1s delay

// Alarm callback
bool timer_callback(struct repeating_timer *t) {
    // Called periodically
    return true;  // Keep repeating
}

// Setup repeating timer (1000ms interval)
struct repeating_timer timer;
add_repeating_timer_ms(1000, timer_callback, NULL, &timer);

// Cancel timer
cancel_repeating_timer(&timer);
```

### Watchdog Timer

Resets the system if not refreshed within timeout period.

```c
#include "hardware/watchdog.h"

// Enable watchdog (timeout in milliseconds)
watchdog_enable(1000, false);

// Refresh watchdog
watchdog_update();

// Triggered reset (with optional delay)
watchdog_reboot(0, 0, 0);
```

### Always-On Timer (AON)

Low-power timer that runs even in deep sleep.

```c
#include "hardware/powman.h"

// Read AON timer (64-bit, 1ms resolution by default)
uint64_t time_ms = powman_timer_get_ms();

// Set alarm (wake from sleep)
powman_timer_set_alarm_ms(time_ms + 5000);  // Alarm in 5 seconds
```

## Clocks

### Clock Architecture

The RP2350 has multiple clock sources and generators:

**Clock Sources:**
- XOSC: External crystal oscillator (1-50 MHz, typically 12 MHz)
- ROSC: Internal ring oscillator (~6.5 MHz, variable)
- LPOSC: Low-power oscillator (~32 kHz)
- PLL_SYS: System PLL (from XOSC)
- PLL_USB: USB PLL (48 MHz from XOSC)

**Clock Generators:**
- `clk_sys`: System clock (CPU, bus, most peripherals)
- `clk_peri`: Peripheral clock (UART, SPI, I2C, PWM)
- `clk_usb`: USB clock (48 MHz required)
- `clk_adc`: ADC clock
- `clk_ref`: Reference clock (always on)

**Default Configuration:**
- System clock: 150 MHz (from PLL_SYS)
- USB clock: 48 MHz (from PLL_USB)
- Peripheral clock: 150 MHz (same as system)

**Setting System Clock:**

```c
#include "hardware/clocks.h"

// Initialize clocks to default (150 MHz)
clocks_init();

// Set specific frequency
set_sys_clock_khz(150000, true);  // 150 MHz

// Overclock (use with caution, test for stability)
set_sys_clock_khz(200000, false);  // 200 MHz (not guaranteed)

// Get current frequency
uint32_t freq = clock_get_hz(clk_sys);
```

### PLL Configuration

```c
#include "hardware/pll.h"

// PLL calculation:
// PLL_freq = (XOSC_freq / REFDIV) × FBDIV / POSTDIV1 / POSTDIV2

// Example: 12 MHz XOSC -> 150 MHz
// 12 MHz / 1 × 125 / 5 / 2 = 150 MHz
pll_init(pll_sys, 1, 1500 * MHZ, 5, 2);
```

## Interrupts

### NVIC (Nested Vectored Interrupt Controller)

The Cortex-M33 has a standard NVIC with:
- 52 peripheral interrupts
- 16 priority levels (4 bits)
- Nested interrupt support

**Setting up interrupts:**

```c
#include "hardware/irq.h"

// Interrupt handler
void my_irq_handler(void) {
    // Handle interrupt
    // Clear interrupt flag in peripheral
}

// Enable interrupt
irq_set_enabled(UART0_IRQ, true);

// Set handler
irq_set_exclusive_handler(UART0_IRQ, my_irq_handler);

// Set priority (0 = highest, 255 = lowest)
irq_set_priority(UART0_IRQ, 0x40);

// Enable/disable all interrupts
irq_set_mask_enabled(0xFFFFFFFF, true);
```

**Critical Sections:**

```c
#include "hardware/sync.h"

// Save interrupt state and disable
uint32_t state = save_and_disable_interrupts();

// Critical code here

// Restore previous state
restore_interrupts(state);
```

## Multicore Programming

### Core 1 Launch

The RP2350 has two cores that can run independently.

```c
#include "pico/multicore.h"

// Core 1 entry point
void core1_main() {
    // Core 1 code here
    while (1) {
        tight_loop_contents();
    }
}

// Launch core 1 from core 0
multicore_launch_core1(core1_main);

// Reset core 1
multicore_reset_core1();
```

### Inter-core Communication

**FIFOs:**

```c
// Core 0 sends to core 1
multicore_fifo_push_blocking(0x12345678);

// Core 1 receives from core 0
uint32_t data = multicore_fifo_pop_blocking();

// Check FIFO status
if (multicore_fifo_rvalid()) {
    uint32_t data = multicore_fifo_pop_blocking();
}
```

**Spinlocks:**

```c
#include "hardware/sync.h"

// Claim spinlock
uint spin_lock_num = spin_lock_claim_unused(true);

// Acquire lock
uint32_t state = spin_lock_blocking(spin_lock_num);

// Critical section

// Release lock
spin_unlock(spin_lock_num, state);
```

**Mutexes (SDK provided):**

```c
#include "pico/mutex.h"

mutex_t my_mutex;

// Initialize
mutex_init(&my_mutex);

// Lock
mutex_enter_blocking(&my_mutex);

// Critical section

// Unlock
mutex_exit(&my_mutex);
```

## Flash Memory

### Flash Programming

**Warning:** Programming flash while executing from it requires special handling.

```c
#include "hardware/flash.h"
#include "hardware/sync.h"

// Flash operations MUST disable interrupts
// Code MUST NOT execute from flash during operations
// Use SRAM-resident code or SDK functions

#define FLASH_TARGET_OFFSET (256 * 1024)  // 256KB offset

// Erase sector (4096 bytes minimum)
uint32_t ints = save_and_disable_interrupts();
flash_range_erase(FLASH_TARGET_OFFSET, 4096);
restore_interrupts(ints);

// Program page (256 bytes)
uint8_t data[256] = {...};
ints = save_and_disable_interrupts();
flash_range_program(FLASH_TARGET_OFFSET, data, 256);
restore_interrupts(ints);

// Read (direct memory access)
const uint8_t *flash_ptr = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
uint8_t value = flash_ptr[0];
```

### Flash Safety

1. **Disable interrupts** during flash operations
2. **Don't execute from flash** during programming/erase
3. **Both cores must stop** accessing flash
4. Use SDK functions - they handle the complexity
5. Erase before programming
6. Sector size: 4096 bytes
7. Page size: 256 bytes
8. Minimum write: 1 byte (but page programming is faster)

## Power Management

### Power States

- **RUN**: Normal operation, all peripherals available
- **SLEEP**: CPU stopped, peripherals running, wake on interrupt
- **DORMANT**: Most clocks stopped, wake on GPIO or RTC
- **POWEROFF**: Minimal power, wake on GPIO only (if supported by board)

**Sleep Mode:**

```c
#include "hardware/sync.h"

// Enter sleep (wake on any interrupt)
__wfi();  // Wait For Interrupt

// Or use SDK
#include "pico/sleep.h"
sleep_ms(1000);  // Efficient sleep
```

**Deep Sleep (Dormant):**

```c
#include "hardware/xosc.h"
#include "hardware/rosc.h"
#include "hardware/clocks.h"

// Prepare for dormant mode
// This will stop most clocks
// Wake only on GPIO edge or RTC alarm

// Setup GPIO wake source
gpio_set_dormant_irq_enabled(PIN_NUMBER, 
    GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);

// Enter dormant
xosc_dormant();  // Stops XOSC, enters very low power state

// Execution continues here after wake
clocks_init();  // Re-initialize clocks after wake
```

## Security Features (Arm TrustZone)

### Secure Boot

The RP2350 supports secure boot with signature verification.

**OTP Configuration:**
- Store public key in OTP
- Enable secure boot in OTP
- Sign binaries with private key

**Security States:**
- **Secure**: Full access to all resources
- **Non-secure**: Limited access, enforced by hardware

### Memory Protection

**MPU (Memory Protection Unit):**

```c
// ARM MPU configuration (low-level)
// Usually handled by SDK or RTOS

// Example: Protect a memory region
// Requires ARM CMSIS headers
// Configure MPU regions for:
// - Code protection
// - Data protection
// - Stack protection
// - Peripheral access control
```

**TrustZone Considerations:**
- Peripherals can be assigned to Secure or Non-secure domains
- Memory regions can be Secure or Non-secure
- Code can transition between states via special instructions
- Most applications won't use TrustZone unless security-critical

## Bootrom and Boot Process

### Boot Sequence

1. **Power-on**: Bootrom starts executing
2. **Boot mode selection**: Checks BOOTSEL button and OTP
3. **Flash boot**: Tries to boot from flash (default)
4. **USB boot**: If flash fails or BOOTSEL held, enters USB bootloader

### BOOTSEL Mode

Enter BOOTSEL by:
- Holding BOOTSEL button during power-on
- No valid program in flash
- Program calls `reset_usb_boot()`

In BOOTSEL mode:
- RP2350 appears as USB mass storage device
- Drag and drop .UF2 files to program
- Or use `picotool` for advanced operations

**Programmatically enter BOOTSEL:**

```c
#include "pico/bootrom.h"

// Reboot to BOOTSEL mode
reset_usb_boot(0, 0);
```

### Bootrom API

The bootrom provides utility functions:

```c
#include "pico/bootrom.h"

// Flash erase/program functions
rom_flash_range_erase(...);
rom_flash_range_program(...);

// Cryptographic functions
rom_sha256(...);

// Other utilities
rom_get_partition_table_info(...);
```

## SDK Initialization

### Standard Initialization Pattern

```c
#include "pico/stdlib.h"

int main() {
    // Initialize all standard subsystems
    stdio_init_all();  // USB and UART stdio
    
    // Your code here
    while (1) {
        // Main loop
    }
    
    return 0;
}
```

**What `stdio_init_all()` does:**
- Initializes USB CDC serial
- Initializes default UART (GPIO 0/1)
- Sets up printf/scanf redirection
- Enables USB and UART for stdin/stdout

**Minimal Initialization:**

```c
#include "pico/stdlib.h"

int main() {
    // No initialization needed for basic GPIO
    // For other peripherals, initialize explicitly:
    
    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
    
    // Your code
}
```

## Standard I/O

### printf Support

```c
#include <stdio.h>
#include "pico/stdlib.h"

int main() {
    stdio_init_all();
    
    printf("Hello, World!\n");
    printf("Value: %d\n", 42);
    
    // Input
    char buffer[128];
    fgets(buffer, sizeof(buffer), stdin);
}
```

**Where printf goes:**
- USB CDC serial (if connected)
- UART0 (GPIO 0/1)
- Both by default

**USB-only or UART-only:**

```c
stdio_usb_init();   // USB only
stdio_uart_init();  // UART only
```

## CMake and Build System

### Typical CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.13)

# Include SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(my_project C CXX ASM)

# Initialize the SDK
pico_sdk_init()

# Create executable
add_executable(my_project
    main.c
)

# Link libraries
target_link_libraries(my_project
    pico_stdlib
    hardware_gpio
    hardware_uart
    hardware_spi
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_dma
)

# Enable USB output, disable UART output
pico_enable_stdio_usb(my_project 1)
pico_enable_stdio_uart(my_project 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(my_project)
```

### Common SDK Libraries

- `pico_stdlib`: Standard library (includes most common needs)
- `hardware_gpio`: GPIO functions
- `hardware_uart`: UART functions
- `hardware_spi`: SPI functions
- `hardware_i2c`: I2C functions
- `hardware_pwm`: PWM functions
- `hardware_adc`: ADC functions
- `hardware_dma`: DMA functions
- `hardware_pio`: PIO functions
- `hardware_timer`: Timer functions
- `hardware_flash`: Flash programming
- `pico_multicore`: Multicore support
- `pico_time`: Time and alarm functions

## Common Patterns and Best Practices

### Efficient GPIO Toggle

```c
// Slow (SDK function call)
gpio_put(pin, !gpio_get(pin));

// Fast (direct register access)
GPIO_OUT_XOR = (1u << pin);
```

### Busy-Wait with Timeout

```c
#include "pico/time.h"

absolute_time_t timeout = make_timeout_time_ms(1000);

while (!condition) {
    if (time_reached(timeout)) {
        // Timeout occurred
        return ERROR_TIMEOUT;
    }
    tight_loop_contents();
}
```

### DMA for Performance

Use DMA for:
- Large memory copies
- Peripheral data transfers
- Background operations

Avoid DMA for:
- Small transfers (overhead not worth it)
- Infrequent operations
- Simple byte moves

### Interrupt vs Polling

**Use Interrupts:**
- Asynchronous events (GPIO edges)
- Latency-critical responses
- When CPU can do other work

**Use Polling:**
- High-frequency sampling
- Simple, fast checks
- When interrupt latency is unacceptable

### Memory Alignment

```c
// Aligned allocation for DMA
uint8_t __attribute__((aligned(4))) dma_buffer[256];

// Or use SDK
#include "pico/platform.h"
uint8_t buffer[256] __attribute__((aligned(4)));
```

## Debugging

### SWD Debug

The RP2350 supports SWD (Serial Wire Debug):

**SWD Pins:**
- SWCLK: GPIO 2 (or GPIO 26 on some boards)
- SWDIO: GPIO 3 (or GPIO 27 on some boards)

**Debug Probe Options:**
- Raspberry Pi Debug Probe
- Another Pico running picoprobe
- CMSIS-DAP compatible probes
- J-Link probes

### Printf Debugging

```c
#include <stdio.h>

// Debug output
printf("Debug: x=%d, y=%d\n", x, y);

// Hex dump
for (int i = 0; i < len; i++) {
    printf("%02X ", buffer[i]);
}
printf("\n");
```

### LED Blink (Alive Check)

```c
#define LED_PIN 25  // or PICO_DEFAULT_LED_PIN

gpio_init(LED_PIN);
gpio_set_dir(LED_PIN, GPIO_OUT);

while (1) {
    gpio_put(LED_PIN, 1);
    sleep_ms(250);
    gpio_put(LED_PIN, 0);
    sleep_ms(250);
}
```

## Differences from RP2040

Key changes programmers should know:

1. **More Memory**: 520KB SRAM vs 264KB
2. **Faster**: 150 MHz vs 133 MHz default
3. **More GPIO**: Up to 48 vs 30
4. **More ADC Channels**: Up to 8 vs 4
5. **Security Features**: TrustZone, secure boot (new)
6. **Dual Architecture**: Can run Arm OR RISC-V
7. **Three PIO Blocks**: PIO0, PIO1, PIO2 (vs 2 blocks)
8. **HSTX Peripheral**: High-speed TX for displays (new)
9. **SHA-256 Accelerator**: Hardware crypto (new)
10. **OTP Memory**: 8KB user-programmable (expanded)

**Code Compatibility:**
- Most RP2040 code works unchanged
- Recompile with RP2350 target
- Take advantage of new features where needed
- Check for deprecated APIs in SDK

## Register Access (Advanced)

### Direct Register Access

```c
#include "hardware/regs/addressmap.h"
#include "hardware/regs/sio.h"

// Example: Direct GPIO set
volatile uint32_t *gpio_out_set = 
    (volatile uint32_t *)(SIO_BASE + SIO_GPIO_OUT_SET_OFFSET);
*gpio_out_set = (1u << PIN_NUMBER);

// Using SDK macros (preferred)
#include "hardware/structs/sio.h"
sio_hw->gpio_out_set = (1u << PIN_NUMBER);
```

### Peripheral Base Addresses

```c
#define UART0_BASE  0x40034000
#define UART1_BASE  0x40038000
#define SPI0_BASE   0x4003C000
#define SPI1_BASE   0x40040000
#define I2C0_BASE   0x40044000
#define I2C1_BASE   0x40048000
#define ADC_BASE    0x4004C000
#define PWM_BASE    0x40050000
#define TIMER_BASE  0x40054000
#define DMA_BASE    0x50000000
#define PIO0_BASE   0x50200000
#define PIO1_BASE   0x50300000
#define PIO2_BASE   0x50400000
#define SIO_BASE    0xD0000000
```

## Critical Performance Tips

1. **Use DMA for bulk transfers** - Frees CPU for other tasks
2. **PIO for timing-critical protocols** - Offloads bit-banging
3. **Multicore for parallelism** - Double your processing power
4. **Direct register access for speed** - Bypass SDK overhead when needed
5. **Interrupt-driven I/O** - Don't waste cycles polling
6. **Optimize clock speeds** - Slow down unused peripherals
7. **Use hardware divider** - Faster than software division
8. **Cache-aware code** - Keep hot code in first 16KB of function
9. **SRAM over flash** - Place performance-critical code in RAM
10. **Atomic operations** - Use `_SET`, `_CLR`, `_XOR` registers

## Error Handling

### Common Patterns

```c
// Return codes
#define SUCCESS 0
#define ERROR_TIMEOUT -1
#define ERROR_INVALID -2

int my_function(void) {
    if (error_condition) {
        return ERROR_INVALID;
    }
    return SUCCESS;
}

// Assertions
#include <assert.h>
assert(pointer != NULL);

// SDK panic
#include "pico/error.h"
if (critical_error) {
    panic("Critical error message");
}
```

## Quick Reference: Common Tasks

### Blink LED
```c
gpio_init(LED_PIN);
gpio_set_dir(LED_PIN, GPIO_OUT);
while (1) {
    gpio_put(LED_PIN, !gpio_get(LED_PIN));
    sleep_ms(500);
}
```

### Read Button
```c
gpio_init(BUTTON_PIN);
gpio_set_dir(BUTTON_PIN, GPIO_IN);
gpio_pull_up(BUTTON_PIN);
if (!gpio_get(BUTTON_PIN)) {
    // Button pressed (active low)
}
```

### Serial Output
```c
stdio_init_all();
printf("Hello, World!\n");
```

### PWM LED Fade
```c
gpio_set_function(LED_PIN, GPIO_FUNC_PWM);
uint slice = pwm_gpio_to_slice_num(LED_PIN);
pwm_set_wrap(slice, 255);
pwm_set_enabled(slice, true);
for (int level = 0; level < 256; level++) {
    pwm_set_gpio_level(LED_PIN, level);
    sleep_ms(10);
}
```

### Read ADC
```c
adc_init();
adc_gpio_init(26);
adc_select_input(0);
uint16_t value = adc_read();
float voltage = value * 3.3f / 4096.0f;
```

## Conclusion

The RP2350 is a powerful, flexible microcontroller with extensive peripheral support. Key strengths:

- **Fast and efficient**: 150 MHz dual-core with large memory
- **Flexible I/O**: PIO allows custom protocols without CPU overhead
- **Rich peripherals**: Everything you need for embedded projects
- **Excellent SDK**: Clean, well-documented C/C++ API
- **Security options**: Secure boot for production devices
- **Low cost**: ~$0.80-$1.00 in volume

For most applications, the Pico SDK provides everything needed. Direct register access is available when maximum performance is required.

### Further Resources

- **Pico SDK Documentation**: https://www.raspberrypi.com/documentation/pico-sdk/
- **RP2350 Datasheet**: https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf
- **Hardware Design Guide**: https://datasheets.raspberrypi.com/rp2350/hardware-design-with-rp2350.pdf
- **Pico Examples**: https://github.com/raspberrypi/pico-examples
- **Getting Started Guide**: https://www.raspberrypi.com/documentation/microcontrollers/

This reference covers the essentials for C/C++ development on the RP2350. Refer to the full datasheet for electrical specifications, timing diagrams, and complete register maps.

# RP2350 Complete Pinout Reference

## Package Overview

The RP2350 comes in two package options:

- **RP2350A / RP2354A**: QFN-60 package, 7mm × 7mm, 30 GPIO pins
- **RP2350B / RP2354B**: QFN-80 package, 10mm × 10mm, 48 GPIO pins

*Note: RP2354 variants include 2MB internal flash; RP2350 variants require external flash*

---

## RP2350A / RP2354A Pinout (QFN-60)

### Pin Configuration Table

| Pin | Name | Type | Description | Functions |
|-----|------|------|-------------|-----------|
| 1 | IOVDD | Power | Digital IO supply (1.8-3.3V) | Power |
| 2 | GPIO0 | I/O | GPIO Bank 0, Pin 0 | SPI0 RX, UART0 TX, I2C0 SDA, PWM0 A, PIO0/1/2, USB OVCUR DET |
| 3 | GPIO1 | I/O | GPIO Bank 0, Pin 1 | SPI0 CSn, UART0 RX, I2C0 SCL, PWM0 B, PIO0/1/2, USB VBUS DET |
| 4 | GPIO2 | I/O | GPIO Bank 0, Pin 2 | SPI0 SCK, UART0 CTS, I2C1 SDA, PWM1 A, PIO0/1/2 |
| 5 | GPIO3 | I/O | GPIO Bank 0, Pin 3 | SPI0 TX, UART0 RTS, I2C1 SCL, PWM1 B, PIO0/1/2 |
| 6 | GPIO4 | I/O | GPIO Bank 0, Pin 4 | SPI0 RX, UART1 TX, I2C0 SDA, PWM2 A, PIO0/1/2, USB OVCUR DET |
| 7 | GPIO5 | I/O | GPIO Bank 0, Pin 5 | SPI0 CSn, UART1 RX, I2C0 SCL, PWM2 B, PIO0/1/2, USB VBUS DET |
| 8 | GPIO6 | I/O | GPIO Bank 0, Pin 6 | SPI0 SCK, UART1 CTS, I2C1 SDA, PWM3 A, PIO0/1/2, USB VBUS EN |
| 9 | GPIO7 | I/O | GPIO Bank 0, Pin 7 | SPI0 TX, UART1 RTS, I2C1 SCL, PWM3 B, PIO0/1/2 |
| 10 | IOVDD | Power | Digital IO supply (1.8-3.3V) | Power |
| 11 | GPIO8 | I/O | GPIO Bank 0, Pin 8 | SPI1 RX, UART1 TX, I2C0 SDA, PWM4 A, PIO0/1/2, USB OVCUR DET |
| 12 | GPIO9 | I/O | GPIO Bank 0, Pin 9 | SPI1 CSn, UART1 RX, I2C0 SCL, PWM4 B, PIO0/1/2, USB VBUS DET |
| 13 | GPIO10 | I/O | GPIO Bank 0, Pin 10 | SPI1 SCK, UART1 CTS, I2C1 SDA, PWM5 A, PIO0/1/2, USB VBUS EN |
| 14 | GPIO11 | I/O | GPIO Bank 0, Pin 11 | SPI1 TX, UART1 RTS, I2C1 SCL, PWM5 B, PIO0/1/2 |
| 15 | GPIO12 | I/O | GPIO Bank 0, Pin 12 | SPI1 RX, UART0 TX, I2C0 SDA, PWM6 A, PIO0/1/2 |
| 16 | GPIO13 | I/O | GPIO Bank 0, Pin 13 | SPI1 CSn, UART0 RX, I2C0 SCL, PWM6 B, PIO0/1/2 |
| 17 | GPIO14 | I/O | GPIO Bank 0, Pin 14 | SPI1 SCK, UART0 CTS, I2C1 SDA, PWM7 A, PIO0/1/2 |
| 18 | GPIO15 | I/O | GPIO Bank 0, Pin 15 | SPI1 TX, UART0 RTS, I2C1 SCL, PWM7 B, PIO0/1/2 |
| 19 | TESTEN | Input | Test enable (tie to GND) | Test mode control |
| 20 | XIN | Input | Crystal oscillator input | XOSC input (12MHz typical) |
| 21 | XOUT | Output | Crystal oscillator output | XOSC output |
| 22 | IOVDD | Power | Digital IO supply (1.8-3.3V) | Power |
| 23 | DVDD | Power | Digital core supply (1.1V) | Power (from VREG or external) |
| 24 | SWCLK | I/O | Serial Wire Debug Clock | Debug interface |
| 25 | SWDIO | I/O | Serial Wire Debug Data I/O | Debug interface |
| 26 | GPIO16 | I/O | GPIO Bank 0, Pin 16 | SPI0 RX, UART0 TX, I2C0 SDA, PWM0 A, PIO0/1/2 |
| 27 | GPIO17 | I/O | GPIO Bank 0, Pin 17 | SPI0 CSn, UART0 RX, I2C0 SCL, PWM0 B, PIO0/1/2 |
| 28 | GPIO18 | I/O | GPIO Bank 0, Pin 18 | SPI0 SCK, UART0 CTS, I2C1 SDA, PWM1 A, PIO0/1/2 |
| 29 | GPIO19 | I/O | GPIO Bank 0, Pin 19 | SPI0 TX, UART0 RTS, I2C1 SCL, PWM1 B, PIO0/1/2 |
| 30 | GPIO20 | I/O | GPIO Bank 0, Pin 20 | SPI0 RX, UART1 TX, I2C0 SDA, PWM2 A, PIO0/1/2, HSTX 0 |
| 31 | GPIO21 | I/O | GPIO Bank 0, Pin 21 | SPI0 CSn, UART1 RX, I2C0 SCL, PWM2 B, PIO0/1/2, HSTX 1 |
| 32 | IOVDD | Power | Digital IO supply (1.8-3.3V) | Power |
| 33 | GPIO22 | I/O | GPIO Bank 0, Pin 22 | SPI0 SCK, UART1 CTS, I2C1 SDA, PWM3 A, PIO0/1/2, HSTX 2 |
| 34 | GPIO23 | I/O | GPIO Bank 0, Pin 23 | SPI0 TX, UART1 RTS, I2C1 SCL, PWM3 B, PIO0/1/2, HSTX 3 |
| 35 | GPIO24 | I/O | GPIO Bank 0, Pin 24 | SPI1 RX, UART1 TX, I2C0 SDA, PWM4 A, PIO0/1/2, HSTX 4 |
| 36 | GPIO25 | I/O | GPIO Bank 0, Pin 25 | SPI1 CSn, UART1 RX, I2C0 SCL, PWM4 B, PIO0/1/2, HSTX 5 |
| 37 | GPIO26 | I/O | GPIO Bank 0, Pin 26 | SPI1 SCK, UART1 CTS, I2C1 SDA, PWM5 A, PIO0/1/2, HSTX 6, ADC 0 |
| 38 | GPIO27 | I/O | GPIO Bank 0, Pin 27 | SPI1 TX, UART1 RTS, I2C1 SCL, PWM5 B, PIO0/1/2, HSTX 7, ADC 1 |
| 39 | GPIO28 | I/O | GPIO Bank 0, Pin 28 | SPI0 RX, UART0 TX, I2C0 SDA, PWM6 A, PIO0/1/2, ADC 2 |
| 40 | GPIO29 | I/O | GPIO Bank 0, Pin 29 | SPI0 CSn, UART0 RX, I2C0 SCL, PWM6 B, PIO0/1/2, ADC 3 |
| 41 | IOVDD | Power | Digital IO supply (1.8-3.3V) | Power |
| 42 | ADC_AVDD | Power | ADC power supply (3.3V) | Power for ADC |
| 43 | VREG_IN | Power | Voltage regulator input (2.7-5.5V) | Input to on-chip SMPS |
| 44 | VREG_AVDD | Power | Voltage regulator analog supply | SMPS analog supply |
| 45 | VREG_LX | Output | Voltage regulator inductor connection | SMPS switching node |
| 46 | VREG_PGND | Ground | Voltage regulator power ground | SMPS power ground |
| 47 | USB_OTP_VDD | Power | USB PHY and OTP supply (3.3V) | Power for USB PHY |
| 48 | USB_DM | I/O | USB D- data line | USB differential pair |
| 49 | USB_DP | I/O | USB D+ data line | USB differential pair |
| 50 | GND | Ground | Digital ground | Ground |
| 51 | QSPI_SD3 | I/O | QSPI Data 3 | Flash/PSRAM interface |
| 52 | QSPI_SCLK | Output | QSPI Clock | Flash/PSRAM clock |
| 53 | QSPI_SD0 | I/O | QSPI Data 0 (MOSI) | Flash/PSRAM interface |
| 54 | QSPI_SD2 | I/O | QSPI Data 2 | Flash/PSRAM interface |
| 55 | QSPI_SD1 | I/O | QSPI Data 1 (MISO) | Flash/PSRAM interface |
| 56 | QSPI_SS | Output | QSPI Chip Select | Flash/PSRAM select |
| 57 | QSPI_IOVDD | Power | QSPI IO supply (1.8-3.3V) | Power for QSPI pins |
| 58 | DVDD | Power | Digital core supply (1.1V) | Power (from VREG or external) |
| 59 | RUN | Input | Chip enable (active high) | Reset control input |
| 60 | GND | Ground | Digital ground | Ground |
| EP | GND | Ground | Exposed pad (ground) | Ground (must be connected) |

### RP2350A GPIO Summary

**Total GPIO pins:** 30 (GPIO0-GPIO29)  
**ADC capable pins:** 4 (GPIO26-GPIO29)  
**HSTX capable pins:** 8 (GPIO20-GPIO27)

---

## RP2350B / RP2354B Pinout (QFN-80)

### Pin Configuration Table

| Pin | Name | Type | Description | Functions |
|-----|------|------|-------------|-----------|
| 1 | GPIO0 | I/O | GPIO Bank 0, Pin 0 | SPI0 RX, UART0 TX, I2C0 SDA, PWM0 A, PIO0/1/2, USB OVCUR DET |
| 2 | GPIO1 | I/O | GPIO Bank 0, Pin 1 | SPI0 CSn, UART0 RX, I2C0 SCL, PWM0 B, PIO0/1/2, USB VBUS DET |
| 3 | GPIO2 | I/O | GPIO Bank 0, Pin 2 | SPI0 SCK, UART0 CTS, I2C1 SDA, PWM1 A, PIO0/1/2 |
| 4 | GPIO3 | I/O | GPIO Bank 0, Pin 3 | SPI0 TX, UART0 RTS, I2C1 SCL, PWM1 B, PIO0/1/2 |
| 5 | GPIO4 | I/O | GPIO Bank 0, Pin 4 | SPI0 RX, UART1 TX, I2C0 SDA, PWM2 A, PIO0/1/2, USB OVCUR DET |
| 6 | GPIO5 | I/O | GPIO Bank 0, Pin 5 | SPI0 CSn, UART1 RX, I2C0 SCL, PWM2 B, PIO0/1/2, USB VBUS DET |
| 7 | IOVDD | Power | Digital IO supply (1.8-3.3V) | Power |
| 8 | GPIO6 | I/O | GPIO Bank 0, Pin 6 | SPI0 SCK, UART1 CTS, I2C1 SDA, PWM3 A, PIO0/1/2, USB VBUS EN |
| 9 | GPIO7 | I/O | GPIO Bank 0, Pin 7 | SPI0 TX, UART1 RTS, I2C1 SCL, PWM3 B, PIO0/1/2 |
| 10 | GPIO8 | I/O | GPIO Bank 0, Pin 8 | SPI1 RX, UART1 TX, I2C0 SDA, PWM4 A, PIO0/1/2, USB OVCUR DET |
| 11 | GPIO9 | I/O | GPIO Bank 0, Pin 9 | SPI1 CSn, UART1 RX, I2C0 SCL, PWM4 B, PIO0/1/2, USB VBUS DET |
| 12 | GPIO10 | I/O | GPIO Bank 0, Pin 10 | SPI1 SCK, UART1 CTS, I2C1 SDA, PWM5 A, PIO0/1/2, USB VBUS EN |
| 13 | GPIO11 | I/O | GPIO Bank 0, Pin 11 | SPI1 TX, UART1 RTS, I2C1 SCL, PWM5 B, PIO0/1/2 |
| 14 | IOVDD | Power | Digital IO supply (1.8-3.3V) | Power |
| 15 | GPIO12 | I/O | GPIO Bank 0, Pin 12 | SPI1 RX, UART0 TX, I2C0 SDA, PWM6 A, PIO0/1/2 |
| 16 | GPIO13 | I/O | GPIO Bank 0, Pin 13 | SPI1 CSn, UART0 RX, I2C0 SCL, PWM6 B, PIO0/1/2 |
| 17 | GPIO14 | I/O | GPIO Bank 0, Pin 14 | SPI1 SCK, UART0 CTS, I2C1 SDA, PWM7 A, PIO0/1/2 |
| 18 | GPIO15 | I/O | GPIO Bank 0, Pin 15 | SPI1 TX, UART0 RTS, I2C1 SCL, PWM7 B, PIO0/1/2 |
| 19 | GPIO16 | I/O | GPIO Bank 0, Pin 16 | SPI0 RX, UART0 TX, I2C0 SDA, PWM8 A, PIO0/1/2 |
| 20 | GPIO17 | I/O | GPIO Bank 0, Pin 17 | SPI0 CSn, UART0 RX, I2C0 SCL, PWM8 B, PIO0/1/2 |
| 21 | IOVDD | Power | Digital IO supply (1.8-3.3V) | Power |
| 22 | GPIO18 | I/O | GPIO Bank 0, Pin 18 | SPI0 SCK, UART0 CTS, I2C1 SDA, PWM9 A, PIO0/1/2 |
| 23 | GPIO19 | I/O | GPIO Bank 0, Pin 19 | SPI0 TX, UART0 RTS, I2C1 SCL, PWM9 B, PIO0/1/2 |
| 24 | GPIO20 | I/O | GPIO Bank 0, Pin 20 | SPI0 RX, UART1 TX, I2C0 SDA, PWM10 A, PIO0/1/2, HSTX 0 |
| 25 | GPIO21 | I/O | GPIO Bank 0, Pin 21 | SPI0 CSn, UART1 RX, I2C0 SCL, PWM10 B, PIO0/1/2, HSTX 1 |
| 26 | GPIO22 | I/O | GPIO Bank 0, Pin 22 | SPI0 SCK, UART1 CTS, I2C1 SDA, PWM11 A, PIO0/1/2, HSTX 2 |
| 27 | GPIO23 | I/O | GPIO Bank 0, Pin 23 | SPI0 TX, UART1 RTS, I2C1 SCL, PWM11 B, PIO0/1/2, HSTX 3 |
| 28 | IOVDD | Power | Digital IO supply (1.8-3.3V) | Power |
| 29 | GPIO24 | I/O | GPIO Bank 0, Pin 24 | SPI1 RX, UART1 TX, I2C0 SDA, PWM12 A, PIO0/1/2, HSTX 4 |
| 30 | GPIO25 | I/O | GPIO Bank 0, Pin 25 | SPI1 CSn, UART1 RX, I2C0 SCL, PWM12 B, PIO0/1/2, HSTX 5 |
| 31 | GPIO26 | I/O | GPIO Bank 0, Pin 26 | SPI1 SCK, UART1 CTS, I2C1 SDA, PWM13 A, PIO0/1/2, HSTX 6, ADC 0 |
| 32 | GPIO27 | I/O | GPIO Bank 0, Pin 27 | SPI1 TX, UART1 RTS, I2C1 SCL, PWM13 B, PIO0/1/2, HSTX 7, ADC 1 |
| 33 | GPIO28 | I/O | GPIO Bank 0, Pin 28 | SPI0 RX, UART0 TX, I2C0 SDA, PWM14 A, PIO0/1/2, ADC 2 |
| 34 | GPIO29 | I/O | GPIO Bank 0, Pin 29 | SPI0 CSn, UART0 RX, I2C0 SCL, PWM14 B, PIO0/1/2, ADC 3 |
| 35 | IOVDD | Power | Digital IO supply (1.8-3.3V) | Power |
| 36 | GPIO30 | I/O | GPIO Bank 0, Pin 30 | SPI0 SCK, UART0 CTS, I2C1 SDA, PWM15 A, PIO0/1/2, ADC 4 |
| 37 | GPIO31 | I/O | GPIO Bank 0, Pin 31 | SPI0 TX, UART0 RTS, I2C1 SCL, PWM15 B, PIO0/1/2, ADC 5 |
| 38 | GPIO32 | I/O | GPIO Bank 0, Pin 32 | SPI0 RX, UART1 TX, I2C0 SDA, PWM0 A, PIO0/1/2, ADC 6 |
| 39 | GPIO33 | I/O | GPIO Bank 0, Pin 33 | SPI0 CSn, UART1 RX, I2C0 SCL, PWM0 B, PIO0/1/2, ADC 7 |
| 40 | GPIO34 | I/O | GPIO Bank 0, Pin 34 | SPI0 SCK, UART1 CTS, I2C1 SDA, PWM1 A, PIO0/1/2 |
| 41 | GPIO35 | I/O | GPIO Bank 0, Pin 35 | SPI0 TX, UART1 RTS, I2C1 SCL, PWM1 B, PIO0/1/2 |
| 42 | IOVDD | Power | Digital IO supply (1.8-3.3V) | Power |
| 43 | GPIO36 | I/O | GPIO Bank 0, Pin 36 | SPI1 RX, UART1 TX, I2C0 SDA, PWM2 A, PIO0/1/2 |
| 44 | GPIO37 | I/O | GPIO Bank 0, Pin 37 | SPI1 CSn, UART1 RX, I2C0 SCL, PWM2 B, PIO0/1/2 |
| 45 | GPIO38 | I/O | GPIO Bank 0, Pin 38 | SPI1 SCK, UART1 CTS, I2C1 SDA, PWM3 A, PIO0/1/2 |
| 46 | GPIO39 | I/O | GPIO Bank 0, Pin 39 | SPI1 TX, UART1 RTS, I2C1 SCL, PWM3 B, PIO0/1/2 |
| 47 | GPIO40 | I/O | GPIO Bank 0, Pin 40 | SPI0 RX, UART0 TX, I2C0 SDA, PWM4 A, PIO0/1/2 |
| 48 | GPIO41 | I/O | GPIO Bank 0, Pin 41 | SPI0 CSn, UART0 RX, I2C0 SCL, PWM4 B, PIO0/1/2 |
| 49 | IOVDD | Power | Digital IO supply (1.8-3.3V) | Power |
| 50 | GPIO42 | I/O | GPIO Bank 0, Pin 42 | SPI0 SCK, UART0 CTS, I2C1 SDA, PWM5 A, PIO0/1/2 |
| 51 | GPIO43 | I/O | GPIO Bank 0, Pin 43 | SPI0 TX, UART0 RTS, I2C1 SCL, PWM5 B, PIO0/1/2 |
| 52 | GPIO44 | I/O | GPIO Bank 0, Pin 44 | SPI1 RX, UART1 TX, I2C0 SDA, PWM6 A, PIO0/1/2 |
| 53 | GPIO45 | I/O | GPIO Bank 0, Pin 45 | SPI1 CSn, UART1 RX, I2C0 SCL, PWM6 B, PIO0/1/2 |
| 54 | GPIO46 | I/O | GPIO Bank 0, Pin 46 | SPI1 SCK, UART1 CTS, I2C1 SDA, PWM7 A, PIO0/1/2 |
| 55 | GPIO47 | I/O | GPIO Bank 0, Pin 47 | SPI1 TX, UART1 RTS, I2C1 SCL, PWM7 B, PIO0/1/2 |
| 56 | IOVDD | Power | Digital IO supply (1.8-3.3V) | Power |
| 57 | ADC_AVDD | Power | ADC power supply (3.3V) | Power for ADC |
| 58 | VREG_IN | Power | Voltage regulator input (2.7-5.5V) | Input to on-chip SMPS |
| 59 | VREG_AVDD | Power | Voltage regulator analog supply | SMPS analog supply |
| 60 | VREG_LX | Output | Voltage regulator inductor connection | SMPS switching node |
| 61 | VREG_PGND | Ground | Voltage regulator power ground | SMPS power ground |
| 62 | USB_OTP_VDD | Power | USB PHY and OTP supply (3.3V) | Power for USB PHY |
| 63 | USB_DM | I/O | USB D- data line | USB differential pair |
| 64 | USB_DP | I/O | USB D+ data line | USB differential pair |
| 65 | GND | Ground | Digital ground | Ground |
| 66 | QSPI_SD3 | I/O | QSPI Data 3 | Flash/PSRAM interface |
| 67 | QSPI_SCLK | Output | QSPI Clock | Flash/PSRAM clock |
| 68 | QSPI_SD0 | I/O | QSPI Data 0 (MOSI) | Flash/PSRAM interface |
| 69 | QSPI_SD2 | I/O | QSPI Data 2 | Flash/PSRAM interface |
| 70 | QSPI_SD1 | I/O | QSPI Data 1 (MISO) | Flash/PSRAM interface |
| 71 | QSPI_SS | Output | QSPI Chip Select | Flash/PSRAM select |
| 72 | QSPI_IOVDD | Power | QSPI IO supply (1.8-3.3V) | Power for QSPI pins |
| 73 | GND | Ground | Digital ground | Ground |
| 74 | DVDD | Power | Digital core supply (1.1V) | Power (from VREG or external) |
| 75 | SWCLK | I/O | Serial Wire Debug Clock | Debug interface |
| 76 | SWDIO | I/O | Serial Wire Debug Data I/O | Debug interface |
| 77 | RUN | Input | Chip enable (active high) | Reset control input |
| 78 | GND | Ground | Digital ground | Ground |
| 79 | TESTEN | Input | Test enable (tie to GND) | Test mode control |
| 80 | XIN | Input | Crystal oscillator input | XOSC input (12MHz typical) |
| 81 | XOUT | Output | Crystal oscillator output | XOSC output |
| EP | GND | Ground | Exposed pad (ground) | Ground (must be connected) |

### RP2350B GPIO Summary

**Total GPIO pins:** 48 (GPIO0-GPIO47)  
**ADC capable pins:** 8 (GPIO26-GPIO33)  
**HSTX capable pins:** 8 (GPIO20-GPIO27)

---

## GPIO Function Selection Reference

Each GPIO pin can be configured for different functions using the GPIO_CTRL register's FUNCSEL field:

### Function Selection Values

| Value | Function | Description |
|-------|----------|-------------|
| 0 | SPI | SPI peripheral (MISO/MOSI/SCK/CS) |
| 1 | UART | UART peripheral (TX/RX/CTS/RTS) |
| 2 | I2C | I2C peripheral (SDA/SCL) |
| 3 | PWM | PWM output channel |
| 4 | SIO | Software controlled I/O (GPIO) |
| 5 | PIO0 | Programmable I/O block 0 |
| 6 | PIO1 | Programmable I/O block 1 |
| 7 | PIO2 | Programmable I/O block 2 |
| 8 | USB | USB functions (specific pins only) |
| 9 | UART (alt) | Alternate UART assignment |
| 11 | HSTX | High-speed transmit (specific pins) |
| 31 | NULL | No function assigned |

### Code Example

```c
#include "hardware/gpio.h"

// Set GPIO0 as UART TX
gpio_set_function(0, GPIO_FUNC_UART);

// Set GPIO20 as PWM output
gpio_set_function(20, GPIO_FUNC_PWM);

// Set GPIO15 as software-controlled GPIO
gpio_set_function(15, GPIO_FUNC_SIO);
gpio_set_dir(15, GPIO_OUT);
```

---

## Peripheral Pin Assignments

### UART Pin Options

#### UART0
| Function | Primary Pins | Alternate Pins |
|----------|--------------|----------------|
| TX | GPIO0, 12, 16, 28 | GPIO8 (alt func) |
| RX | GPIO1, 13, 17, 29 | GPIO9 (alt func) |
| CTS | GPIO2, 14, 18, 30 | GPIO10 (alt func) |
| RTS | GPIO3, 15, 19, 31 | GPIO11 (alt func) |

#### UART1
| Function | Primary Pins | Alternate Pins |
|----------|--------------|----------------|
| TX | GPIO4, 8, 20, 32 | - |
| RX | GPIO5, 9, 21, 33 | - |
| CTS | GPIO6, 10, 22, 34 | - |
| RTS | GPIO7, 11, 23, 35 | - |

### SPI Pin Options

#### SPI0
| Function | Pin Options |
|----------|-------------|
| RX (MISO) | GPIO0, 4, 16, 20, 28, 32, 40 |
| CSn | GPIO1, 5, 17, 21, 29, 33, 41 |
| SCK | GPIO2, 6, 18, 22, 30, 34, 42 |
| TX (MOSI) | GPIO3, 7, 19, 23, 31, 35, 43 |

#### SPI1
| Function | Pin Options |
|----------|-------------|
| RX (MISO) | GPIO8, 12, 24, 36, 44 |
| CSn | GPIO9, 13, 25, 37, 45 |
| SCK | GPIO10, 14, 26, 38, 46 |
| TX (MOSI) | GPIO11, 15, 27, 39, 47 |

### I2C Pin Options

#### I2C0
| Function | Pin Options |
|----------|-------------|
| SDA | GPIO0, 4, 8, 12, 16, 20, 28, 32, 36, 40, 44 |
| SCL | GPIO1, 5, 9, 13, 17, 21, 29, 33, 37, 41, 45 |

#### I2C1
| Function | Pin Options |
|----------|-------------|
| SDA | GPIO2, 6, 10, 14, 18, 22, 26, 30, 34, 38, 42, 46 |
| SCL | GPIO3, 7, 11, 15, 19, 23, 27, 31, 35, 39, 43, 47 |

### PWM Channel Mapping

PWM channels are mapped based on GPIO number:

```
PWM Channel = GPIO_NUM / 2
PWM Output  = (GPIO_NUM % 2) ? B : A

Examples:
GPIO0  -> PWM0 A    GPIO16 -> PWM8 A     GPIO32 -> PWM0 A
GPIO1  -> PWM0 B    GPIO17 -> PWM8 B     GPIO33 -> PWM0 B
GPIO2  -> PWM1 A    GPIO18 -> PWM9 A     GPIO34 -> PWM1 A
GPIO3  -> PWM1 B    GPIO19 -> PWM9 B     GPIO35 -> PWM1 B
...
```

**RP2350A:** 16 PWM channels (PWM0-PWM7 use all outputs, PWM8-PWM15 incomplete)  
**RP2350B:** All 16 PWM channels fully available

### ADC Channel Mapping

| ADC Channel | RP2350A Pin | RP2350B Pin |
|-------------|-------------|-------------|
| ADC 0 | GPIO26 | GPIO26 |
| ADC 1 | GPIO27 | GPIO27 |
| ADC 2 | GPIO28 | GPIO28 |
| ADC 3 | GPIO29 | GPIO29 |
| ADC 4 | - | GPIO30 |
| ADC 5 | - | GPIO31 |
| ADC 6 | - | GPIO32 |
| ADC 7 | - | GPIO33 |
| ADC 4 (A) / ADC 8 (B) | Temperature sensor | Temperature sensor |

### HSTX Pin Mapping

High-Speed Transmit peripheral (for DVI, parallel displays):

| HSTX Bit | GPIO Pin (Both packages) |
|----------|--------------------------|
| HSTX 0 | GPIO20 |
| HSTX 1 | GPIO21 |
| HSTX 2 | GPIO22 |
| HSTX 3 | GPIO23 |
| HSTX 4 | GPIO24 |
| HSTX 5 | GPIO25 |
| HSTX 6 | GPIO26 |
| HSTX 7 | GPIO27 |

---

## Power Supply Pins

### Power Domains

| Pin Name | Voltage | Description | Notes |
|----------|---------|-------------|-------|
| IOVDD | 1.8-3.3V | Digital GPIO power | Multiple pins, connect all together |
| QSPI_IOVDD | 1.8-3.3V | QSPI interface power | Can be different from IOVDD |
| DVDD | 1.1V | Digital core power | From VREG or external, connect all |
| ADC_AVDD | 3.3V | ADC analog power | Clean 3.3V supply |
| USB_OTP_VDD | 3.3V | USB PHY and OTP power | 3.3V required |
| VREG_AVDD | 3.3V | SMPS analog supply | For internal regulator |

### Voltage Regulator Pins

| Pin Name | Description |
|----------|-------------|
| VREG_IN | Input to on-chip SMPS (2.7-5.5V) |
| VREG_LX | Inductor connection (switching node) |
| VREG_PGND | Power ground for SMPS |
| DVDD | Output from VREG (1.1V nominal) |

**Typical external regulator circuit requires:**
- Input capacitor (10µF) on VREG_IN
- Inductor (2.2µH) between VREG_LX and DVDD
- Output capacitor (10µF) on DVDD
- Ground connections on VREG_PGND

---

## Debug and Control Pins

### Serial Wire Debug (SWD)

| Pin Name | Description | Notes |
|----------|-------------|-------|
| SWCLK | Debug clock | Can be GPIO function when not debugging |
| SWDIO | Debug data I/O | Can be GPIO function when not debugging |

**Default SWD pins:**
- **RP2350A**: SWCLK=Pin 24, SWDIO=Pin 25
- **RP2350B**: SWCLK=Pin 75, SWDIO=Pin 76

### Other Control Pins

| Pin Name | Description | Connection |
|----------|-------------|------------|
| RUN | Chip enable/reset | Pull high to run, low to reset. Add RC delay for power-up |
| TESTEN | Test mode enable | **Must connect to GND in normal operation** |
| XIN | Crystal input | Connect to 12MHz crystal |
| XOUT | Crystal output | Connect to 12MHz crystal |

---

## QSPI Flash Interface (Bank 1)

Both packages have identical QSPI pins:

| Pin Name | Function | Description |
|----------|----------|-------------|
| QSPI_SS | Chip select | Active low, controlled by QMI |
| QSPI_SCLK | Clock | Up to 133 MHz |
| QSPI_SD0 | Data 0 | MOSI in SPI mode |
| QSPI_SD1 | Data 1 | MISO in SPI mode |
| QSPI_SD2 | Data 2 | Additional data line in QSPI mode |
| QSPI_SD3 | Data 3 | Additional data line in QSPI mode |

**Typical connections:**
- W25Q series flash memory (Winbond)
- QSPI PSRAM for additional RAM
- Supports SPI, Dual-SPI, and Quad-SPI modes

---

## USB Pins

| Pin Name | Description | Connection |
|----------|-------------|------------|
| USB_DP | USB D+ data | Connect to USB receptacle D+ |
| USB_DM | USB D- data | Connect to USB receptacle D- |

**Notes:**
- Internal 1.1kΩ pull-up resistor on DP (device mode)
- No external resistors needed for basic USB
- USB_OTP_VDD must be 3.3V for USB operation
- 27Ω series resistors recommended on D+/D-

---

## Pin Type Definitions

### I/O Capabilities

**Standard Digital I/O (GPIO):**
- Input with pull-up, pull-down, or bus-keeper
- Output with configurable drive strength (2/4/8/12 mA)
- Schmitt trigger input
- Slew rate control
- Input/output enable

**Special Functions:**
- Some pins have additional analog functions (ADC)
- Some pins connect to specific peripherals (USB, QSPI)
- All user GPIOs can be PIO or software controlled

### Electrical Characteristics (Summary)

| Parameter | Value |
|-----------|-------|
| IOVDD voltage | 1.8V - 3.3V |
| Input high (VIH) | 0.7 × IOVDD |
| Input low (VIL) | 0.3 × IOVDD |
| Output high (VOH) | IOVDD - 0.4V @ 4mA |
| Output low (VOL) | 0.4V @ 4mA |
| Drive strength | 2, 4, 8, 12 mA (selectable) |
| Max current per pin | 12 mA |
| Max total GPIO current | 200 mA |

---

## Quick Reference: Common Configurations

### Raspberry Pi Pico 2 Board Pinout (RP2350A-based)

For reference, the Raspberry Pi Pico 2 development board uses RP2350A with this pinout:

```
              ┌─────────────┐
         GP0 ─┤1    DEBUG  40├─ VBUS
         GP1 ─┤2          39├─ VSYS
         GND ─┤3          38├─ GND
         GP2 ─┤4          37├─ 3V3_EN
         GP3 ─┤5          36├─ 3V3(OUT)
         GP4 ─┤6          35├─                
         GP5 ─┤7          34├─ GP28/ADC2
         GND ─┤8          33├─ GND/AGND
         GP6 ─┤9          32├─ GP27/ADC1
         GP7 ─┤10         31├─ GP26/ADC0
         GP8 ─┤11         30├─ RUN
         GP9 ─┤12         29├─ GP22
         GND ─┤13         28├─ GND
        GP10 ─┤14         27├─ GP21
        GP11 ─┤15         26├─ GP20
        GP12 ─┤16         25├─ GP19
        GP13 ─┤17         24├─ GP18
         GND ─┤18         23├─ GND
        GP14 ─┤19         22├─ GP17
        GP15 ─┤20         21├─ GP16
              └─────────────┘
```

**Pico 2 Specific:**
- LED on GP25 (internal, not exposed on pinout)
- WL_GPIO0 and WL_GPIO1 are for wireless modules (if populated)
- BOOTSEL button (not a GPIO)

---

## Design Guidelines

### Power Supply Decoupling

**Required decoupling capacitors:**
- 100nF ceramic near each IOVDD pin
- 100nF ceramic near DVDD pins
- 100nF ceramic on USB_OTP_VDD
- 100nF ceramic on ADC_AVDD
- 10µF bulk capacitor on IOVDD
- 10µF bulk capacitor on DVDD

### Crystal Oscillator

**Recommended crystal:**
- 12 MHz fundamental mode
- 10-20 pF load capacitance
- Add 20pF capacitors from XIN/XOUT to ground

### Reset Circuit

**RUN pin:**
- Add 100kΩ pull-up to IOVDD
- Add 1µF capacitor to ground
- Optional: Add reset button to pull low

### GPIO Considerations

1. **Maximum current:** 12mA per pin, 200mA total
2. **5V tolerance:** NOT 5V tolerant, use level shifters
3. **Pull resistors:** Internal ~50kΩ (weak), use external for I2C
4. **ADC pins:** Keep analog signals clean, avoid switching nearby
5. **QSPI signals:** Keep traces short, matched length for high-speed

---

## Package Comparison Summary

| Feature | RP2350A (QFN-60) | RP2350B (QFN-80) |
|---------|------------------|------------------|
| **Package size** | 7mm × 7mm | 10mm × 10mm |
| **Total pins** | 60 | 80 |
| **GPIO count** | 30 (GPIO0-29) | 48 (GPIO0-47) |
| **ADC channels** | 4 + temp sensor | 8 + temp sensor |
| **PWM outputs** | 30 (limited channels) | 48 (all 16 channels) |
| **UART/SPI/I2C** | Same functionality | Same functionality |
| **HSTX pins** | 8 pins | 8 pins |
| **IOVDD pins** | 5 | 7 |
| **Cost** | Lower | Higher |
| **Use case** | Standard projects | High I/O count needs |

---

## Code Examples for Pin Configuration

### Set GPIO as Output
```c
#include "hardware/gpio.h"

void setup_output(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);  // Start low
}
```

### Set GPIO as Input with Pull-up
```c
void setup_input(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}
```

### Configure Multiple Pins at Once
```c
// Configure GPIO 0-7 as outputs
for (int i = 0; i < 8; i++) {
    gpio_init(i);
    gpio_set_dir(i, GPIO_OUT);
}
```

### Set Pin Function
```c
// GPIO 0 as UART0 TX
gpio_set_function(0, GPIO_FUNC_UART);

// GPIO 20 as PWM
gpio_set_function(20, GPIO_FUNC_PWM);

// GPIO 4 as I2C0 SDA
gpio_set_function(4, GPIO_FUNC_I2C);
gpio_pull_up(4);  // I2C needs pull-ups
```

---

This pinout reference provides complete pin-by-pin details for both RP2350 packages. Refer to the full datasheet for electrical specifications, timing requirements, and PCB layout guidelines.
