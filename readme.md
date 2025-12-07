# pixel_blit

A  32-channel addressable LED controller based on the Raspberry Pi RP2350B microcontroller.

## Overview

The pixel_blit is designed for controlling large-scale addressable LED installations. It features LVDS networking for daisy-chaining multiple controllers across long cable runs, making it ideal for large lighting installations.

## Key Specifications

- **Microcontroller**: Raspberry Pi RP2350B (QFN-80)
  - Dual Cortex-M33 cores @ 150 MHz
  - 8 PIO state machines for precise LED timing
  - USB High Speed (480 Mbps)

- **Output Channels**: 32 independent LED string outputs
  - 47Ω series resistors on all outputs
  - 3.3V to 5V level shifting
  - Screw terminal connections (3.5mm pitch)

- **Memory**:
  - 16MB QSPI Flash (W25Q128JVSIQTR)
  - MicroSD card slot for pattern storage

- **Power Management**:
  - 12V input via terminal blocks
  - TPS63070 buck-boost converter (12V → 5V, 2A)
  - ME6217C33 LDO regulator (5V → 3.3V, 300mA)

- **Networking**:
  - LVDS daisy-chaining via RJ45 connectors
  - 4-channel differential signaling (DSLVDS1047/1048)
  - 100Ω termination resistors
  - Supports long cable runs

- **User Interface**:
  - MicroSD card slot
  - 4 tactile buttons + 1 reset button
  - 4-position DIP switch
  - I2C display interface

## Board Details

- **PCB Size**: 175.5mm × 122.0mm
- **Layers**: 4-layer stackup (Signal/GND/Power/Signal)
- **Components**: 192 total (all SMT, single-sided assembly)
- **Connectors**:
  - 32× DB301V 3.5mm screw terminals (LED outputs)
  - 4× power input terminals (12V)
  - 2× RJ45 (LVDS networking)
  - 1× USB-C

## Architecture

### Signal Path
```
RP2350B GPIO (3.3V)
  → SN74LVC8T245 Level Shifter (4× 8-channel)
  → 47Ω Series Resistor
  → Screw Terminal
  → LED String
```

### Power Distribution
```
12V Input
  → TPS63070 Buck-Boost → 5V (LED outputs, level shifters)
  → ME6217C33 LDO → 3.3V (MCU, flash, LVDS)
  → RP2350B Internal → 1.1V
```

### Network Topology
```
[Controller 1] —RJ45→ Cat5/Cat6 —RJ45→ [Controller 2] —RJ45→ ...
```
LVDS differential signaling enables daisy-chaining multiple boards with deterministic latency and excellent noise immunity.

## Component Summary

| Type | Count | Examples |
|------|-------|----------|
| Resistors | 65 | 47Ω series resistors, 100Ω LVDS termination |
| Capacitors | 45 | Decoupling and power filtering |
| ICs | 50+ | MCU, flash, level shifters, LVDS transceivers |
| Terminal Blocks | 36 | 32 LED outputs + 4 power inputs |
| Level Shifters | 4 | SN74LVC8T245PWR (8-channel each) |
| Switches/Buttons | 5 | Tactile buttons + DIP switch |
| LVDS Transceivers | 2 | Driver and receiver pair |

## Features

- 32 independent addressable LED channels
- 800kHz data rate (WS2812B protocol)
- USB programming and control interface
- Offline pattern storage via MicroSD
- Long-distance networking via LVDS over Cat5/Cat6
- Complete user interface for standalone operation

## Software Architecture

The firmware uses a **Redux-inspired unidirectional data flow** pattern for state management. This separates pure state logic from hardware side effects, making the code testable and predictable.

```
Hardware Events → Actions → Pure Reducer → New State
                                    ↓
                              (if dirty)
                                    ↓
                         SideEffects + View Update
```

Key components:
- **AppState**: Immutable state container with version-based dirty detection
- **Actions**: Immutable event objects representing what happened
- **Reducer**: Pure function for state transitions (no side effects)
- **SideEffects**: Isolated module for all hardware I/O

See [docs/reactive_architecture.md](docs/reactive_architecture.md) for detailed documentation.


## Pinout

### Legend

| Prefix | Description |
|--------|-------------|
| String* | LED data output |
| HS_DATA* | Interboard LVDS communication lines |
| SD_* | SD Card interface |

### GPIO Assignments

| GPIO | Function | Direction | Notes |
|------|----------|-----------|-------|
| 0-31 | String0-String31 | Output | LED data outputs |
| 32 | HS_DATA3 | OUT/IN | Output if board address = 0, else input |
| 33 | HS_DATA2 | OUT/IN | Output if board address = 0, else input |
| 34 | HS_DATA1 | OUT/IN | Output if board address = 0, else input |
| 35 | HS_DATA0 | OUT/IN | Output if board address = 0, else input |
| 36 | SD_MISO | Input | SD Card data in |
| 37 | SD_CS# | Output | SD Card chip select |
| 38 | SD_SCK | Output | SD Card clock |
| 39 | SD_MOSI | Output | SD Card data out |
| 40/ADC0 | Board Address | Analog In | DIP switch voltage divider |
| 41/ADC1 | Audio Level | Analog In | Audio input level |
| 42 | IR In | Digital In | Infrared receiver |
| 43 | Select BTN | Digital In | Select button (active low) |
| 44 | DATA_IN_EN | Digital Out | HIGH if board address != 0 |
| 45 | Next BTN | Digital In | Next button (active low) |
| 46 | DISP_SDA | Digital Out | I2C display data |
| 47 | DISP_SCL | Digital Out | I2C display clock |

### Board Address DIP Switch

The board address is set via a 4-position DIP switch using an analog voltage divider:

```
3.3V ──┬── 47kΩ ──┬── GPIO40/ADC0
       │          │
       │    ┌─ SW1 ─── 47kΩ  ──┐
       │    ├─ SW2 ─── 100kΩ ──┤
       │    ├─ SW3 ─── 220kΩ ──┼── GND
       │    └─ SW4 ─── 470kΩ ──┘
       │          │
       └── 1kΩ ───┘
```

Each switch combination produces a unique analog voltage that can be decoded to determine the board address (0-15).


## Prototypes


Directory 'cblinken' contains code for the WS2811 pio programs, and a prototype for IR input decoding.

Directory 'board address prototype' contains code for decoding the board address logic, along with some design info.

Directory 'lvds' contains prototype information on the high speed 4 wire inter board communication protocol, which will be outboard from board zero, inbound to board address > 0.



## License

MIT License
