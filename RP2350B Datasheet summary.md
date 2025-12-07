# RP2350B Developer's Summary (QFN-80 Package)

**Datasheet Version:** d126e9e-clean (Build Date: 2025-07-29)

---

## Quick Reference

| Feature | Specification |
|---------|--------------|
| **Package** | QFN-80 (10 × 10 mm) |
| **Cores** | Dual Cortex-M33 **or** Dual Hazard3 (RISC-V) |
| **Clock Speed** | Up to 150 MHz |
| **GPIO** | 48 user GPIO (Bank 0: GPIO0-47) |
| **SRAM** | 520 KB in 10 independent banks |
| **ROM** | 64 KB bootrom |
| **Flash** | None onboard (use external QSPI) |
| **ADC** | 8 channels (12-bit, 500 kSPS) on GPIO40-47 |
| **PIO** | 3 blocks × 4 state machines = 12 total |
| **PWM** | 12 channels (24 outputs) |
| **USB** | 1.1 Full Speed host/device |
| **Temperature** | -40°C to +85°C |

---

## Package Differences: RP2350A vs RP2350B

| Feature | RP2350A (QFN-60) | RP2350B (QFN-80) |
|---------|------------------|------------------|
| **Size** | 7 × 7 mm | 10 × 10 mm |
| **GPIO Count** | 30 (GPIO0-29) | **48 (GPIO0-47)** |
| **ADC Channels** | 4 (GPIO26-29) | **8 (GPIO40-47)** |
| **PWM Channels** | 8 × 2 = 16 outputs | **12 × 2 = 24 outputs** |

**Key Point:** RP2350B provides **18 additional GPIOs** (GPIO30-47) compared to RP2350A.

---

## GPIO Mapping (QFN-80 Specific)

### Bank 0 GPIO (All 48 pins available in QFN-80)

**GPIO0-29:** Available in both QFN-60 and QFN-80
**GPIO30-47:** **QFN-80 ONLY** ⚠️

### ADC Channels (QFN-80)

| GPIO | ADC Channel |
|------|-------------|
| GPIO40 | ADC0 |
| GPIO41 | ADC1 |
| GPIO42 | ADC2 |
| GPIO43 | ADC3 |
| GPIO44 | ADC4 |
| GPIO45 | ADC5 |
| GPIO46 | ADC6 |
| GPIO47 | ADC7 |

**ADC Specs:**
- Resolution: 12-bit (0-4095) - **Must call `analogReadResolution(12)`!** (Arduino defaults to 10-bit)
- Sample rate: 500 kSPS max
- Input range: 0-3.3V (ADC_AVDD)
- Reference: Internal or external via ADC_AVDD

---

## Processor Architecture

### Dual Architecture Support

RP2350 uniquely supports **both** Arm and RISC-V:
- **Cortex-M33:** Industry-standard Arm v8-M with TrustZone
- **Hazard3:** Raspberry Pi's open-source RISC-V processor

**Architecture Selection:**
- Bootrom detects binary type automatically
- Can run mixed: Core 0 = Arm, Core 1 = RISC-V (or vice versa)
- Set at boot via image metadata

### Cortex-M33 Features

- **ARMv8-M Mainline** with DSP extensions
- **TrustZone-M** security
- **Single-precision FPU** (IEEE 754)
- **Custom coprocessors:**
  - **GPIOC:** Fast GPIO sampling (all 48 GPIOs in one instruction)
  - **DCP:** Double-precision floating-point
  - **RCP:** Redundant computation for safety-critical code

### Hazard3 (RISC-V) Features

- **RV32IMAZCb_Zba_Zbb_Zbs_Zbkb** base
- Custom extensions for RP2350 integration
- Comparable performance to Cortex-M33
- Open-source ISA

---

## Memory Architecture

### SRAM (520 KB Total)

Split into **10 independent 52 KB banks**:
- Banks can be powered down individually
- Striped across banks for performance (reduce contention)
- **ECC protection** available

**Address Map:**
- `0x20000000 - 0x20082000`: Main SRAM (520 KB)
- Accessible to both cores, DMA, and peripherals

### External Flash (XIP)

- **QSPI interface** (up to Quad SPI)
- **Execute-in-Place (XIP)** at `0x10000000`
- **16 KB cache** (8-way set associative)
- Supports up to 16 MB addressing
- **QMI (QSPI Memory Interface):** New high-performance controller

---

## PIO (Programmable IO)

### Overview

**12 state machines total** = 3 PIO blocks × 4 SMs each

### Key Features

- Up to **150 MHz** instruction execution (system clock)
- **32 instructions per SM** (4 words × 8 inst)
- **DMA integration** for zero-CPU streaming
- **Deterministic timing** (cycle-accurate IO)
- **GPIO mapping:** All 48 GPIOs accessible (limited to 32 simultaneous per PIO block)

### PIO Instructions (Selected)

```
out pins, 8    ; Output 8 bits to GPIO pins (single cycle)
pull           ; Pull from TX FIFO
set pins, N    ; Set pins to immediate value
jmp            ; Conditional/unconditional jump
```

### PIO GPIO Base (Critical for GPIO 32+)

Each PIO block can only access a **32-pin window** of GPIOs. The window is controlled by `pio_set_gpio_base()`:

```c
// Window options (only two choices):
// Base 0:  GPIO 0-31
// Base 16: GPIO 16-47

// Example: Access GPIO 38-45
pio_set_gpio_base(pio1, 16);  // Now PIO1 sees GPIO 16-47
sm_config_set_out_pins(&config, 38, 8);  // Use absolute GPIO numbers
```

⚠️ **Without setting GPIO base to 16, PIO cannot access GPIO 32+!**

### Cross-PIO Features (New in RP2350)

- **CTRL.NEXT_PIO_MASK / PREV_PIO_MASK:** Control multiple PIO blocks
- **Synchronized start:** Launch SMs across PIO blocks simultaneously
- **IRQ flags:** Observe IRQ from other PIO blocks (zero delay)

---

## Peripherals

### Communication Interfaces

| Peripheral | Count | Max Speed | Notes |
|------------|-------|-----------|-------|
| **UART** | 2 | 921.6 kbaud | Full modem control |
| **SPI** | 2 | 62.5 MHz | Master/slave |
| **I2C** | 2 | 1 MHz (Fast+) | Master/slave, 10-bit addr |
| **USB** | 1 | 12 Mbps | Host & device |
| **HSTX** | 1 | - | High-speed serial TX (HDMI/DVI) |

### DMA

- **16 channels**
- Chained transfers
- Ring buffers
- Paced by peripherals (UART, SPI, PIO, etc.)
- **Sniff hardware:** CRC on-the-fly

### Timers

- **1 × 64-bit timer:** 1 MHz tick, 4 alarms
- **1 × Watchdog timer**
- **1 × Systick** (Cortex-M33 only)
- **RISC-V platform timer:** 64-bit

### PWM

**12 slices** (QFN-80) = **24 independent outputs**

- 16-bit resolution (wrap value)
- Programmable clock divider
- Phase-correct mode
- Interrupt on wrap

---

## Clocking

### Clock Sources

1. **XOSC:** Crystal oscillator (1-15 MHz external crystal)
2. **ROSC:** Ring oscillator (1.8-12 MHz, on-chip)
3. **PLL_SYS:** Main system PLL (up to 150 MHz)
4. **PLL_USB:** Fixed 48 MHz for USB/ADC

### Clock Tree

```
XOSC (12 MHz) ──→ PLL_SYS ──→ clk_sys (150 MHz max)
              └─→ PLL_USB ──→ clk_usb (48 MHz)
                           └─→ clk_adc (48 MHz)
```

---

## Power

### Supply Rails

| Rail | Voltage | Function |
|------|---------|----------|
| **IOVDD** | 1.8-3.3V | GPIO bank 0 (all 48 GPIOs) |
| **QSPI_IOVDD** | 1.8-3.3V | QSPI flash pins |
| **DVDD** | 1.10V | Digital core (from VREG) |
| **USB_OTP_VDD** | 3.3V | USB PHY and OTP |
| **ADC_AVDD** | 3.3V | ADC analog supply |
| **VREG_VIN** | 1.8-3.6V | Input to onboard regulator |
| **VREG_AVDD** | 1.1V | Analog supply for VREG |

### Power States

- **RUN:** Full operation
- **SLEEP:** Cores halted, peripherals running
- **DORMANT:** Oscillators off, wake on GPIO edge
- **POWER DOWN:** Memory retention off

**Typical Current:**
- Active (150 MHz, both cores): ~50 mA
- Sleep (peripherals on): ~5 mA
- Dormant: ~180 µA

---

## Security Features

### TrustZone-M (Arm Only)

- Secure/Non-secure partitioning
- Per-peripheral security assignment
- Per-GPIO security assignment
- Secure boot with signature verification

### Boot Security

- **Signed boot:** ECDSA-256 or Ed25519
- **Encrypted boot:** AES-128 CTR
- **Hash-based:** SHA-256
- **OTP keys:** One-time programmable secure storage
- **Anti-rollback:** Version tracking

---

## Boot Process

### Boot Sequence

1. **Power-on Reset**
2. **Bootrom** executes (64 KB ROM)
3. Check **BOOTSEL** button
4. If BOOTSEL held → **USB/UART boot**
5. Else → Try **flash boot**
6. Load and verify image
7. Jump to user code

### Boot Sources

- **Flash (XIP):** Primary boot method
- **USB MSC:** Drag-and-drop UF2 files
- **USB PICOBOOT:** Low-level programming interface
- **UART:** Minimal shell for flashless systems
- **OTP:** One-time programmable memory
- **RAM:** Direct load to SRAM

**UF2 Format:** Supported for easy programming

---

## Pin Functions (GPIO30-47)

### High GPIO Pin Capabilities

| GPIO | Available Functions |
|------|---------------------|
| GP30-GP37 | PIO/SPI/I2C/PWM |
| GP38-GP39 | PIO/SPI/I2C/PWM |
| GP40 | PIO/SPI/I2C/PWM/**ADC0** |
| GP41 | PIO/SPI/I2C/PWM/**ADC1** |
| GP42 | PIO/SPI/I2C/PWM/**ADC2** |
| GP43 | PIO/SPI/I2C/PWM/**ADC3** |
| GP44 | PIO/SPI/I2C/PWM/**ADC4** |
| GP45 | PIO/SPI/I2C/PWM/**ADC5** |
| GP46 | PIO/SPI/I2C/PWM/**ADC6** |
| GP47 | PIO/SPI/I2C/PWM/**ADC7** |

**Note:** GP38-45 provides 8 contiguous pins ideal for PIO parallel output (`out pins, 8`).

---

## Electrical Specifications

### Absolute Maximum Ratings

| Parameter | Min | Max | Unit |
|-----------|-----|-----|------|
| IOVDD | -0.3 | 3.6 | V |
| ADC_AVDD | -0.3 | 3.6 | V |
| VREG_VIN | -0.3 | 3.6 | V |
| GPIO Input | -0.3 | IOVDD+0.3 | V |
| Storage Temp | -40 | +125 | °C |

### Operating Conditions

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| IOVDD | 1.8 | 3.3 | 3.3 | V |
| DVDD | 1.08 | 1.10 | 1.12 | V |
| Temperature | -40 | 25 | +85 | °C |
| System Clock | - | - | 150 | MHz |

### GPIO Characteristics

| Parameter | Condition | Min | Typ | Max | Unit |
|-----------|-----------|-----|-----|-----|------|
| Output High | IOH = -4 mA | IOVDD-0.4 | - | - | V |
| Output Low | IOL = 4 mA | - | - | 0.4 | V |
| Input High | | 0.7×IOVDD | - | - | V |
| Input Low | | - | - | 0.3×IOVDD | V |
| Pull-up/down | | 50 | 60 | 70 | kΩ |
| Drive Strength | Programmable | 2 | 4, 8, 12 | 12 | mA |

**GPIO Speed (QFN-80):**
- System clock to GPIO output: **4.1 ns typ, 5.4 ns max**
- Bank 0 GPIO output rise/fall: **1280 ps typ, 2100 ps max** (at 12 mA drive)

---

## Programming & Debug

### Debug Interface

- **SWD (Serial Wire Debug)** on SWCLK/SWDIO
- Supports both Arm (Cortex-M33) and RISC-V (Hazard3)
- **Rescue reset:** Force bootloader entry via SWD
- **Trace:** Optional SWO output

### SDK Support

- **Pico SDK:** Official C/C++ SDK
- **MicroPython:** Full support
- **CircuitPython:** Community support
- **Arduino:** Community support (including RP2350)

**Build Tools:**
- CMake-based (Pico SDK)
- PlatformIO (Arduino)

---

## Critical Gotchas & Notes

### 1. ADC Resolution Default
⚠️ **Arduino framework defaults to 10-bit ADC** (0-1023), not 12-bit!
```cpp
analogReadResolution(12);  // MUST call in setup()!
```

### 2. Package-Specific ADC Pins
⚠️ **RP2350A (QFN-60):** ADC on GPIO26-29 (4 channels)
⚠️ **RP2350B (QFN-80):** ADC on GPIO40-47 (8 channels) ← **Different!**

### 3. GPIO30-47 Only on QFN-80
Many online examples assume QFN-60 (30 GPIO). GPIO30-47 are **QFN-80 exclusive**.

### 4. 3.3V Peripherals
⚠️ Many OLED displays and sensors are **3.3V only** - connecting 5V will damage them. Always check datasheets.

### 5. PIO Pin Mapping & GPIO Base
PIO can access all 48 GPIOs, but limited to **32 simultaneous** per PIO block.
⚠️ **For GPIO 32+, you MUST call `pio_set_gpio_base(pio, 16)`!**
Default base is 0, which only sees GPIO 0-31.

### 6. Flash Required
RP2350B has **no onboard flash** - external QSPI flash is required for XIP boot. RP2354B variant includes 2 MB flash.

### 7. Architecture Selection
Binary must declare Arm or RISC-V in metadata. SDK handles this automatically. Mixed-architecture multicore is possible.

---

## Performance Metrics

### PIO Maximum Throughput

- **PIO clock:** 150 MHz max (system clock)
- **Parallel 8-bit output:** 150 MS/s theoretical (one sample per clock)
- **Practical limit:** ~50-150 MS/s (depends on external circuit settling, DMA, memory bandwidth)

### DMA Bandwidth

- **System bus:** 32-bit @ 150 MHz
- **Peak DMA:** 600 MB/s (limited by memory arbitration)
- **Sustained (single channel):** ~300-400 MB/s

### GPIO Performance

- **Direct GPIO toggle:** ~37.5 MHz max (2 cycles per toggle @ 150 MHz)
- **PIO parallel output:** 150 MS/s theoretical
- **System clock to GPIO propagation:** 4.1 ns typ

---

## Comparison: RP2040 vs RP2350B

| Feature | RP2040 | RP2350B (QFN-80) |
|---------|--------|------------------|
| Cores | 2× Cortex-M0+ | 2× Cortex-M33 or Hazard3 |
| Clock | 133 MHz | 150 MHz |
| SRAM | 264 KB | **520 KB** |
| GPIO | 30 | **48** |
| ADC | 4 channels | **8 channels** |
| PIO | 8 SMs (2×4) | **12 SMs (3×4)** |
| PWM | 8 slices | **12 slices** |
| Security | None | **TrustZone, Secure Boot** |
| FPU | None (M0+) | **Single-precision (M33)** |
| USB | Device only | **Host & Device** |
| Package | QFN-56 | **QFN-80** |

---

## References

- **Datasheet:** RP-008373-DS-2-rp2350-datasheet.pdf
- **Pico SDK:** https://github.com/raspberrypi/pico-sdk
- **Arduino Support:** Platform: `raspberrypi/raspberrypi`

---

*Document Created: November 30, 2025*
*Based on RP2350 Datasheet build d126e9e-clean*
