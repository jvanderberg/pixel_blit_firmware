# Claude Instructions for Rust Firmware

## Build Commands

```bash
# Run tests (on host, no hardware)
cargo test --no-default-features --features view-test --target aarch64-apple-darwin

# Build firmware
cargo build --release

# Check size
arm-none-eabi-size target/thumbv8m.main-none-eabihf/release/pixel_blit_firmware

# Flash
./flash.sh
```

## Architecture

Redux-style firmware using `reducto` crate.

| Module | Purpose |
|--------|---------|
| `state.rs` | AppState and all state structs |
| `action.rs` | Action enum for all events |
| `reducer.rs` | Pure state transitions (no side effects!) |
| `storage.rs` | Flash persistence, CRC32, debounced saves |
| `display.rs` | Display trait + MockDisplay for testing |
| `sdcard.rs` | SD card driver, config.csv parsing |
| `led_driver/` | Token-based double buffering (see README.md) |
| `views/` | Display rendering |

## Feature Flags

- `firmware` (default) - Embassy, hardware drivers
- `view-test` - embedded-graphics for MockDisplay

## Testing

Tests run on host without firmware features:
- `reducer_tests.rs` - State transitions
- `storage.rs` - CRC32, settings validation
- `led_driver/buffer.rs` - Bit-plane encoding
- `led_driver/color.rs` - HSV conversion
- `views/` - View rendering to MockDisplay

## Key Patterns

### Reducer Must Be Pure
No hardware access, no I/O, no timers in reducer. Side effects go in main loop after dispatch.

### Token-Based Buffers
LED driver uses `WriteToken`/`ReadToken` passed through channels. See `led_driver/buffer.rs` and README.md for details.

### Flash Persistence
- Location: Last 4KB sector (offset 0x3FF000)
- 2-second debounce before writing
- CRC32 with polynomial 0xEDB88320

### Spawn Tasks Gracefully
```rust
match my_task(args) {
    Ok(token) => spawner.spawn(token),
    Err(e) => defmt::error!("Failed: {:?}", e),
}
```
