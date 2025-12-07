# Code Review: pb_led_driver Frame Buffer and WS2811 PIO/DMA

**Date:** 2025-12-06
**Reviewed:** `lib/pb_led_driver/` implementation against RP2350 datasheet

## Critical Issue: PIO Timing Off By 1 Cycle

**File:** `lib/pb_led_driver/ws2811_parallel.pio`

```asm
.wrap_target
bitloop:
    out x, 32                   ; 1 cycle ← Not accounted for!
    mov pins, !null     [T1-1]  ; 1 + 2 = 3 cycles
    mov pins, x         [T2-1]  ; 1 + 2 = 3 cycles
    mov pins, null      [T3-1]  ; 1 + 3 = 4 cycles
.wrap
```

**Problem:** Loop is **11 cycles**, but clock divider uses **10 cycles** (T1+T2+T3):

```c
int cycles_per_bit = ws2811_parallel_T1 + ws2811_parallel_T2 + ws2811_parallel_T3;  // = 10
float div = clock_get_hz(clk_sys) / (freq * cycles_per_bit);
```

**Impact:** Bit timing is 10% slow (1.375µs instead of 1.25µs), which may cause LED communication failures.

**Fix:** Restructure PIO to use `out pins` directly:

```asm
.wrap_target
    mov pins, !null     [T1-1]  ; 3 cycles - all HIGH
    out pins, 32        [T2-1]  ; 3 cycles - data (autopull refills OSR)
    mov pins, null      [T3-1]  ; 4 cycles - all LOW
.wrap
```

This eliminates the extra cycle and achieves correct 10-cycle timing.

---

## Minor Issues

### 1. Unnecessary SET Pins Configuration

**File:** `ws2811_parallel.pio:50`

```c
sm_config_set_set_pins(&c, pin_base, pin_count);  // Not used
```

`MOV PINS` uses OUT pin mapping, not SET. This line is dead code.

### 2. Buffer Swap Ordering

**File:** `pb_led_driver.c:289-296`

```c
void pb_show(pb_driver_t* driver) {
    driver->current_buffer ^= 1;   // Swap before waiting
    pb_hw_show(driver, true);
```

The buffer swap happens before `pb_hw_show()` acquires the semaphore. If called rapidly, there's a brief window where the "front buffer" pointer could change while the previous DMA is finishing. Consider swapping after the semaphore acquire.

---

## Design: Correct ✓

### DMA Chain Architecture
The two-channel DMA chain (main + chain) is correctly designed:
- Chain channel's `trans_count=1` reloads on each trigger
- Main chains to chain after each 8-word fragment
- Chain advances through fragment list, triggering main with each new address
- NULL terminator ends the sequence

### Bit-Plane Encoding
Efficient design - encode on write, DMA-ready format, 25% smaller than raw RGB.

### Double Buffering
Proper separation of front (DMA) and back (write) buffers with semaphore synchronization.

---

## Summary

| Area | Status |
|------|--------|
| PIO timing | **Bug** - 11 cycles vs 10 expected |
| DMA chain | ✓ Correct |
| Bit-plane encoding | ✓ Correct |
| Double buffering | ✓ Correct (minor ordering concern) |
| Raster abstraction | ✓ Correct |

**Priority fix:** Restructure PIO program to eliminate the extra `out x, 32` cycle.
