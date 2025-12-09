# TODO

1. ~~Add a global 'on/off' flag. When 'off', the display should turn off, any button should wake it up, and show the main menu. Add a menu item 'shutdown.' When in the off state, all polling is halted but for the two buttons, and all LED output is stopped. Add a handler for the power button on the IR remote to toggle this.~~ ✅

2. ~~Add an IR button handler for the 'next' button which plays the next show on the SD card. If no show is playing, it should load the list and play the first one, wrap around at the end. The display should keep up to date with this, even if the IR remote is doing the commands, so you will need to do 'dispatches', or have special reducer logic to update menu state.~~ ✅

3. ~~Add a global brightness setting, defaulted to 255, which gets mixed in at the pb layer. The IR remote brightness buttons should increment and decrement this value in 10 discrete steps. Add a 'brightness' menu this allows you to cycle through 1-10 brightness.~~ ✅

4. ~~Add SD card configuration file (`config.csv`) for board/string layout.~~ ✅

5. Add EEPROM/flash settings persistence. Save user settings (brightness level, last played file, etc.) to non-volatile storage so they survive power cycles. Consider using RP2350 flash or external EEPROM.

6. Remove board address ADC resistor ladder logic. Instead, set board ID via config.csv or through a menu item in the user interface.

7. Persist config.csv to EEPROM/flash. If config.csv is not present on SD card at boot, load the previously saved configuration from non-volatile storage.

8. Add a "Board Config" menu to display the current board configuration (board ID, string count, per-string pixel counts and color orders).

9. Apply per-string color order in pb_set_pixel. Currently color order is set per-driver, but each string may have a different color order in config.csv. This allows users to use pure RGB in xLights while PB handles the per-string color reordering.

## Future Improvements

### Consistent Error Handling Pattern

Currently, most functions silently return on error (e.g., `pb_set_pixel()` ignores invalid indices). Consider adding a consistent error handling pattern:

**Proposed approach:**
```c
typedef enum {
    PB_OK = 0,
    PB_ERR_NULL_PARAM,
    PB_ERR_INVALID_CONFIG,
    PB_ERR_OUT_OF_BOUNDS,
    PB_ERR_ALREADY_INIT,
    PB_ERR_HW_INIT_FAILED,
    // ...
} pb_error_t;

// Functions return error codes
pb_error_t pb_set_pixel(pb_driver_t* driver, uint8_t board, uint8_t string,
                        uint16_t pixel, pb_color_t color);

// Callers check and propagate
pb_error_t err = pb_set_pixel(driver, 0, 0, 0, color);
if (err != PB_OK) {
    // Handle or propagate
}
```

**Benefits:**
- Explicit error handling at call sites
- Errors can propagate up the call stack
- Easier debugging (know exactly where failures occur)

**Considerations:**
- Larger refactor touching many files
- Some hot paths may not want error checking overhead
- Could use debug-only asserts for performance-critical paths
