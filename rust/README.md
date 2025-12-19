# Pixel Blit Firmware - Rust/Embassy Port

Rust firmware for the Pixel Blit 32-channel LED controller using Embassy async framework on RP2350B.

## Quick Reference

| Script | Description |
|--------|-------------|
| [`./test.sh`](test.sh) | Run reducer tests on host (no hardware needed) |
| [`./build.sh`](build.sh) | Build firmware and convert to UF2 |
| [`./flash.sh`](flash.sh) | Build, convert, and flash to device |

## Project Structure

```
src/
├── lib.rs              # Library root (exports testable modules)
├── main.rs             # Firmware entry point
├── state.rs            # AppState and types
├── action.rs           # Action enum
├── reducer.rs          # Pure state transitions
├── reducer_tests.rs    # Host-runnable tests
├── usb.rs              # USB CDC for bootloader
└── views/              # Display views
    ├── mod.rs          # RootView dispatcher
    ├── main_menu.rs
    ├── info.rs
    ├── board_address.rs
    ├── brightness.rs
    ├── rainbow_test.rs
    ├── string_length.rs
    ├── sd_card.rs
    └── shutdown.rs
```

## Prerequisites

- Rust toolchain with thumbv8m.main-none-eabihf target
- picotool (for flashing via UF2)
- arm-none-eabi-gcc (for objdump/size tools)

```bash
rustup target add thumbv8m.main-none-eabihf
brew install picotool  # macOS
```

## Building

```bash
./build.sh
```

Or manually:
```bash
cargo build --release
```

## Testing

Tests run on host (no hardware needed):

```bash
./test.sh
```

Tests are in [`src/reducer_tests.rs`](src/reducer_tests.rs) and cover:
- Menu navigation (cycling, enter/exit detail views)
- Board address, brightness, rainbow test, string length
- Info view scrolling, shutdown confirmation
- Power state (on/off, wake on button)
- Timer (uptime increment)

## Flashing

```bash
./flash.sh
```

Or manually:
```bash
cargo build --release
picotool uf2 convert target/thumbv8m.main-none-eabihf/release/pixel_blit_firmware \
    -t elf target/pixel_blit_firmware.uf2 --family rp2350-arm-s --abs-block
picotool load target/pixel_blit_firmware.uf2 -f
picotool reboot
```

If the device is running firmware (not in BOOTSEL), hold BOOTSEL and press reset first, or use `-f` flag which forces BOOTSEL mode.

## RP2350 Embassy Setup Notes

### Use Git Version of Embassy

The crates.io version of embassy-rp 0.4 has issues with RP2350 (timer initialization bug). Use the git version:

```toml
embassy-executor = { git = "https://github.com/embassy-rs/embassy", features = ["arch-cortex-m", "executor-thread", "defmt"] }
embassy-time = { git = "https://github.com/embassy-rs/embassy", features = ["defmt"] }
embassy-rp = { git = "https://github.com/embassy-rs/embassy", features = ["rp235xb", "critical-section-impl", "time-driver", "defmt"] }
```

### Memory Layout (memory.x)

RP2350 requires specific linker sections for the boot ROM. The memory.x must include:

```ld
MEMORY {
    FLASH : ORIGIN = 0x10000000, LENGTH = 4096K
    RAM : ORIGIN = 0x20000000, LENGTH = 512K
    SRAM8 : ORIGIN = 0x20080000, LENGTH = 4K
    SRAM9 : ORIGIN = 0x20081000, LENGTH = 4K
}

SECTIONS {
    .start_block : ALIGN(4)
    {
        __start_block_addr = .;
        KEEP(*(.start_block));
        KEEP(*(.boot_info));
    } > FLASH
} INSERT AFTER .vector_table;

_stext = ADDR(.start_block) + SIZEOF(.start_block);

SECTIONS {
    .bi_entries : ALIGN(4)
    {
        __bi_entries_start = .;
        KEEP(*(.bi_entries));
        . = ALIGN(4);
        __bi_entries_end = .;
    } > FLASH
} INSERT AFTER .text;

SECTIONS {
    .end_block : ALIGN(4)
    {
        __end_block_addr = .;
        KEEP(*(.end_block));
    } > FLASH
} INSERT AFTER .uninit;

PROVIDE(start_to_end = __end_block_addr - __start_block_addr);
PROVIDE(end_to_start = __start_block_addr - __end_block_addr);
```

### Build Script (build.rs)

The build.rs must specify the linker scripts:

```rust
use std::env;
use std::fs;
use std::path::PathBuf;

fn main() {
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let memory_x = include_str!("memory.x");
    fs::write(out_dir.join("memory.x"), memory_x).unwrap();

    println!("cargo:rustc-link-search={}", out_dir.display());
    println!("cargo:rustc-link-arg=--nmagic");
    println!("cargo:rustc-link-arg=-Tlink.x");
    println!("cargo:rustc-link-arg=-Tdefmt.x");
    println!("cargo:rerun-if-changed=memory.x");
    println!("cargo:rerun-if-changed=build.rs");
}
```

### UF2 Conversion

RP2350 requires the `--abs-block` flag for the E10 errata workaround:

```bash
picotool uf2 convert firmware.elf -t elf output.uf2 --family rp2350-arm-s --abs-block
```

### Cargo Config (.cargo/config.toml)

```toml
[target.thumbv8m.main-none-eabihf]
runner = "probe-rs run --chip RP235xB"

[build]
target = "thumbv8m.main-none-eabihf"

[env]
DEFMT_LOG = "debug"
```

## Hardware

- **MCU**: RP2350B (Cortex-M33, thumbv8m.main-none-eabihf)
- **GPIO 46-47**: I2C1 for SH1106 OLED (SDA=46, SCL=47)
- **GPIO 0-31**: LED string outputs
- **GPIO 42**: IR receiver
- **GPIO 43**: Select button
- **GPIO 45**: Next button

## Dependencies

- `embassy-rp` - RP2350 HAL with async support
- `embassy-executor` - Async executor
- `embassy-time` - Async timers
- `sh1106` - OLED display driver
- `embedded-graphics` - Graphics primitives
- `reducto` - Redux-like state management (local crate)
- `defmt` + `defmt-rtt` - Logging (requires debug probe)
