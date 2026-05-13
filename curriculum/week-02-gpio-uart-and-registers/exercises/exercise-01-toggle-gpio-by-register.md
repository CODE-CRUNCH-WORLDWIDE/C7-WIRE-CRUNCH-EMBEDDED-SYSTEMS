# Exercise 1 — Toggle a GPIO by Register

**Time estimate:** ~120 minutes (most of it the first disassembly; the code is 30 lines).

## Problem statement

Write a C firmware for the Pi Pico W that toggles GP15 at exactly 1 Hz using **only direct register writes** to the addresses you learned in Lecture 1 — no `gpio_init`, `gpio_put`, or any other `hardware_gpio` call. Then add a second mode that toggles as fast as a tight loop allows, and measure both rates on a logic analyzer. Then disassemble the resulting `.elf` and find the `STR` instruction that targets `SIO_GPIO_OUT_XOR` at `0xd000_001c`.

This is the foundational drill of Week 2. Every later exercise builds on the muscle of "I can spell the address from memory."

## Acceptance criteria

- [ ] A new directory `c7-week02-toggle-by-register/` containing `toggle.c`, `CMakeLists.txt`, and `pico_sdk_import.cmake`.
- [ ] `cmake -B build -DPICO_BOARD=pico_w . && cmake --build build -j` succeeds with no warnings (other than the SDK's own).
- [ ] `toggle.c` contains **no** calls into `hardware/gpio.h` other than (allowed) `gpio_set_function` for the function-select write at boot. All output state changes are direct `*(volatile uint32_t *)` writes to addresses in the `0xd000_0000` SIO bank or the `0x4001_4000` IO_BANK0 bank.
- [ ] Mode 1: GP15 toggles at **1.0 Hz ± 1%**, observed for at least 10 seconds on a logic analyzer.
- [ ] Mode 2: When the board is held in BOOTSEL+restart with GP14 jumpered to GND at boot (or any selector you document), GP15 toggles at the **maximum rate of a tight `while (1)` loop** containing one `SIO_GPIO_OUT_XOR` write. Measure the rate. Expected: 8–25 MHz; document your number.
- [ ] A `notes/toggle-by-register.md` in your repo includes:
  - The full register-write sequence in init (with citations to datasheet page numbers from §2.3 and §2.19).
  - A 4-row table: register name, address, value written, datasheet § / page.
  - One Saleae or PulseView screenshot per mode, showing the measured rate.
  - One paragraph of `arm-none-eabi-objdump -d build/toggle.elf` output containing the `STR` to `0xd000_001c`, annotated.
- [ ] A `notes/registers-touched.md` is started (it will grow over the week): list of every register address you have written to in this exercise, plus one-line description and datasheet citation.

## Hints

<details>
<summary>The C source (`toggle.c`)</summary>

```c
/* C7 · Crunch Wire — Week 02 — Toggle GP15 by direct register write.
 *
 * Hardware: Raspberry Pi Pico W
 * Pin:      GP15, configured as SIO output, drive 4 mA, no pull
 *
 * Build:
 *   cmake -B build -DPICO_BOARD=pico_w .
 *   cmake --build build -j
 * Flash:
 *   drag build/toggle.uf2 to RPI-RP2 in BOOTSEL mode
 *
 * Registers used (citations to RP2040 datasheet, Sep-2024 rev):
 *   GPIO15_CTRL     0x4001_407c  (IO_BANK0 §2.19.6.1, p. 247)
 *   PADS_BANK0_GP15 0x4001_c040  (PADS_BANK0 §2.19.4, p. 294)
 *   SIO_GPIO_OE_SET 0xd000_0024  (SIO       §2.3.1.7, p. 43)
 *   SIO_GPIO_OUT_XOR 0xd000_001c (SIO       §2.3.1.7, p. 42)
 */

#include "pico/stdlib.h"   /* only for sleep_us; nothing GPIO-related */

#define IO_BANK0_BASE   0x40014000u
#define PADS_BANK0_BASE 0x4001c000u
#define SIO_BASE        0xd0000000u

#define GPIO15_CTRL     (*(volatile uint32_t *)(IO_BANK0_BASE   + 0x004u + 15u * 0x008u))
#define PADS_GP15       (*(volatile uint32_t *)(PADS_BANK0_BASE + 0x004u + 15u * 0x004u))
#define SIO_GPIO_OE_SET   (*(volatile uint32_t *)(SIO_BASE + 0x024u))
#define SIO_GPIO_OUT_XOR  (*(volatile uint32_t *)(SIO_BASE + 0x01cu))
#define SIO_GPIO_IN       (*(volatile uint32_t *)(SIO_BASE + 0x004u))

#define GP15_MASK       (1u << 15)
#define GP14_MASK       (1u << 14)

static inline void gp15_output_init(void) {
    /* FUNCSEL = 5 (SIO). Bits [4:0] of GPIO15_CTRL. Datasheet p. 247. */
    GPIO15_CTRL = 5u;
    /* IE=1 (default), DRIVE=2 (8mA), SCHMITT=1, no pull. Datasheet p. 294. */
    PADS_GP15   = (1u << 6) | (2u << 4) | (1u << 1);
    /* Enable output driver. */
    SIO_GPIO_OE_SET = GP15_MASK;
}

static inline void gp14_input_init(void) {
    /* GP14: SIO input with pull-up, used as boot-time mode selector. */
    *(volatile uint32_t *)(IO_BANK0_BASE   + 0x004u + 14u * 0x008u) = 5u;
    *(volatile uint32_t *)(PADS_BANK0_BASE + 0x004u + 14u * 0x004u) =
        (1u << 6) | (1u << 3) | (1u << 1);    /* IE=1, PUE=1, SCHMITT=1 */
    /* OE stays at 0 (high-Z input). */
}

int main(void) {
    stdio_init_all();   /* sets up USB-CDC for printf, optional */
    gp15_output_init();
    gp14_input_init();

    /* Wait a moment for the pull-up to settle the input. */
    sleep_us(10);

    /* Mode select: if GP14 is low at boot, run fast loop. Else 1 Hz. */
    bool fast = ((SIO_GPIO_IN & GP14_MASK) == 0);

    if (fast) {
        /* Tight loop: one XOR per iteration. Expect ~10-25 MHz toggle. */
        while (1) {
            SIO_GPIO_OUT_XOR = GP15_MASK;
        }
    } else {
        /* 1 Hz: 500 ms high, 500 ms low. */
        while (1) {
            SIO_GPIO_OUT_XOR = GP15_MASK;
            sleep_ms(500);
        }
    }

    return 0;
}
```

</details>

<details>
<summary>The `CMakeLists.txt`</summary>

```cmake
cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(toggle C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(toggle toggle.c)

target_link_libraries(toggle pico_stdlib)

pico_enable_stdio_usb(toggle 1)
pico_enable_stdio_uart(toggle 0)

pico_add_extra_outputs(toggle)
```

The `pico_sdk_import.cmake` file is identical to the Week-1 copy; reuse it.

</details>

<details>
<summary>Disassembling and finding the `STR` to 0xd000_001c</summary>

After build:

```bash
arm-none-eabi-objdump -d build/toggle.elf | less
```

Search for the `main` symbol or for the address `d000001c`. You should find a sequence like:

```
   movs    r3, #1
   lsls    r3, r3, #15        ; r3 = 0x8000 = 1 << 15
   ldr     r2, [pc, #20]      ; load 0xd000001c into r2
   str     r3, [r2, #0]       ; *(0xd000001c) = 0x8000
```

The `ldr r2, [pc, #20]` is a PC-relative load of a constant from the literal pool at the end of the function. The constant is `0xd000_001c` — your `SIO_GPIO_OUT_XOR` address, baked into the binary.

Annotate the snippet in your `notes/toggle-by-register.md`. This is the moment "the chip does what I told it" becomes literal.

</details>

<details>
<summary>Measuring the fast-loop rate</summary>

Probe GP15 on a Saleae or PulseView capture at the maximum sample rate your hardware allows. On a Saleae Logic 8 that is 100 MHz. On a $12 sigrok clone that is 24 MHz; that is still enough to see ~10 MHz toggle as a square wave (sample at ≥ 4x the toggled frequency to avoid aliasing).

Expected rate: **8–25 MHz**, depending on the compiler's loop optimization. The instruction sequence is roughly:

```
   1: str    r3, [r2]      ; 1 cycle (SIO write)
      b      1b            ; 1 cycle (taken branch, no pipeline reload)
```

Two cycles per loop iteration → 62.5 MHz toggle in theory, but each toggle is *one* XOR, so the *visible* rate is `62.5 MHz / 2 = 31.25 MHz`. In practice the compiler inserts a delay slot or the silicon adds ~1 cycle of bus latency, so 10–25 MHz is what you'll see.

If you see 1 MHz, you forgot to remove `sleep_us(1)` from the fast path. If you see 60 Hz, your `cmake` build is in Debug and the loop has unoptimized bookkeeping; rebuild with `-DCMAKE_BUILD_TYPE=Release` (the default for `pico-sdk` is already Release, but check).

</details>

## What to capture

In `notes/toggle-by-register.md`, the structure:

```
# Exercise 1 — Toggle GPIO by Register

## Registers touched

| Register | Address | Value written | Datasheet § / page |
|---|---:|---:|---|
| GPIO15_CTRL | 0x4001_407c | 5 (FUNCSEL = SIO) | §2.19.6.1, p. 247 |
| PADS_BANK0_GPIO15 | 0x4001_c040 | 0x52 (IE, DRIVE=8mA, SCHMITT) | §2.19.4, p. 294 |
| SIO_GPIO_OE_SET | 0xd000_0024 | 0x0000_8000 | §2.3.1.7, p. 43 |
| SIO_GPIO_OUT_XOR | 0xd000_001c | 0x0000_8000 (each toggle) | §2.3.1.7, p. 42 |

## Mode 1 — 1 Hz toggle (Saleae screenshot)

[Image: 1 Hz square wave, GP15, 50% duty cycle, measured period 1.000 s ± 0.5 ms]
Sample rate: 4 MHz
Channel: GP15

## Mode 2 — Tight loop (Saleae screenshot)

[Image: ~12.5 MHz square wave]
Sample rate: 100 MHz
Channel: GP15
Measured period: 80 ns → toggle rate 12.5 MHz

## Disassembly of the hot path

[Annotated `objdump -d` output, ~10 lines, showing the STR to 0xd000_001c]

## Reflection

Two paragraphs:
1. Was the measured rate higher or lower than your prediction? Why?
2. What is the smallest change you could make to push it faster? (Hint: `-Os` vs `-O2`; or moving the constant into a register before the loop.)
```

## Stretch goals

- Replace `SIO_GPIO_OUT_XOR` writes with a sequence of `SIO_GPIO_OUT_SET` / `SIO_GPIO_OUT_CLR` writes (two writes per toggle instead of one). Measure the new rate. Confirm it is ~half.
- Use the same approach to drive the on-board LED on Pi Pico W. **Trap:** the on-board LED is on `WL_GPIO0`, behind the CYW43439, not a normal GPIO. You cannot reach it with `SIO_GPIO_OUT_XOR`. You must use `cyw43_arch_gpio_put`. Document this in your notes; this is the Week-1 trap returning.
- Build the same firmware in `-Os` and `-O3` mode and compare the disassembly. The XOR write becomes one instruction either way; the surrounding loop may differ.
- Toggle a second pin (GP13) in the same loop. Measure both — they should be in lock-step. Now toggle them with two separate writes vs one combined write (`SIO_GPIO_OUT_XOR = (1u<<13)|(1u<<15);`). Compare rates.

## Why this matters

This is the exercise the rest of C7 builds on. Every later peripheral — UART, SPI, I2C, ADC, DMA — has the same shape: a base address, a register table, a configuration sequence, an enable bit. If you can write Exercise 1 from memory by Sunday, you can write *any* RP2040 peripheral driver. If you cannot, every later exercise is harder than it needs to be.

Spend the time. The address `0xd000_001c` should be in your fingertips by Sunday evening.
