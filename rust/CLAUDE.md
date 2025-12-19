# Claude Instructions for Rust Firmware

## Build Commands

### Run Tests (on host)
```bash
cargo test --no-default-features --features view-test --target aarch64-apple-darwin
```

### Build Firmware (release)
```bash
cargo build --release
```

### Check Firmware Size
```bash
arm-none-eabi-size target/thumbv8m.main-none-eabihf/release/pixel_blit_firmware
```

### Flash to Device
```bash
./flash.sh
```

## Architecture

This is a Redux-style firmware using the `reducto` crate for state management.

### Key Modules
- `state.rs` - AppState and all state structs
- `action.rs` - Action enum for all events
- `reducer.rs` - Pure state transitions (no side effects)
- `storage.rs` - Flash persistence (FlashSettings)
- `views/` - Display rendering (uses Display trait abstraction)
- `display.rs` - Display trait + MockDisplay for testing

### Feature Flags
- `firmware` (default) - Includes embassy-rp, hardware drivers
- `view-test` - Includes embedded-graphics for view testing

### Testing Strategy
- Tests run on host (macOS) without firmware features
- MockDisplay captures text for view assertions
- Reducer tests verify pure state transitions
- Storage tests verify CRC32 and settings validation

## Flash Persistence

Settings stored in last 4KB sector of 4MB flash:
- Magic: 0x50425345 ("PBSE")
- Version: 3
- Fields: brightness, was_playing, playing_index, auto_loop, board_id
- CRC32 validation (polynomial 0xEDB88320)
- 2-second debounce on saves to reduce flash wear
