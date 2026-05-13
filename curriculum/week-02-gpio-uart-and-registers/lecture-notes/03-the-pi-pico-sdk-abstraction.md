# Lecture 3 — The Pi Pico SDK Abstraction

> **Outcome:** You can open `pico-sdk/src/rp2_common/hardware_gpio/gpio.c`, read the body of `gpio_init` and `gpio_put`, and tell the cohort reviewer which register writes those functions perform. You can disassemble a SDK-built `.elf` with `arm-none-eabi-objdump` and find the `STR` instructions you predicted. You can defend, in one paragraph, when to call `gpio_put` and when to write `SIO_GPIO_OUT_XOR` directly.

---

## 1. Why this lecture exists

In Lecture 1 you wrote `*(volatile uint32_t *)(0xd000_001c) = 1u << 25;` and made a pin toggle. In Lecture 2 you wrote `UART0_IBRD = 67;` and put a UART at 115200 baud. Both worked. Both are reviewable. Both are, in 95% of shipped firmware, the wrong thing to write.

The right thing to write is `gpio_xor_mask(1u << 25);` and `uart_init(uart0, 115200);`. The SDK exists because the alternative is having every C7 graduate ship code that hard-codes register addresses in 80 different files across 12 projects, and one day someone changes the GPIO bank from `IO_BANK0` to a hypothetical `IO_BANK1` on a future RP2350 and everything breaks at once. The SDK is the **one place where the address lives**. Every other call site goes through `gpio_*` and gets the new address for free.

But the SDK is also opaque. You call `gpio_init` and a thing happens. This lecture cracks open `pico-sdk/src/rp2_common/hardware_gpio/gpio.c`, walks the source line by line, disassembles the result, and proves the SDK is doing what you would have done by hand — only more consistently, with the bank-selection logic factored out, and with the right `__compiler_memory_barrier()` calls in the right places.

By the end you will trust the SDK. That trust is the prerequisite for shipping production firmware in Week 11 and beyond.

---

## 2. Reading `gpio_init`

Open the file `src/rp2_common/hardware_gpio/gpio.c` in the `pico-sdk` repo (v1.5.1, the version we pinned in Week 1). The function `gpio_init` is ~10 lines. Annotated, it looks like:

```c
void gpio_init(uint gpio) {
    /* 1. Set OE = 0 (input). Safe default, prevents glitches before
     *    you set the direction explicitly. */
    sio_hw->gpio_oe_clr = 1ul << gpio;
    
    /* 2. Set OUT = 0. Safe default. */
    sio_hw->gpio_clr = 1ul << gpio;
    
    /* 3. Set FUNCSEL = GPIO_FUNC_SIO (= 5) in the per-pin IO_BANK0_CTRL register. */
    gpio_set_function(gpio, GPIO_FUNC_SIO);
}
```

Two of those three lines you wrote in Lecture 1 by hand. The third one (`gpio_set_function`) hides a small but important detail: the SDK's GPIO numbering goes 0..29 for the IO_BANK0 pins, but the same SDK also handles the **QSPI-bank** GPIOs (the pins that talk to the external flash chip) through a parallel API. The function-select register for QSPI pins lives at a different base address. The SDK selects the right one based on the GPIO number.

For GPIO 0..29 (which is everything you can touch on the Pi Pico W carrier), `gpio_set_function` reduces to:

```c
void gpio_set_function(uint gpio, enum gpio_function fn) {
    /* PADS_BANK0_GPIOn: clear OD (output disable), set IE (input enable). */
    hw_write_masked(&pads_bank0_hw->io[gpio],
                    PADS_BANK0_GPIO0_IE_BITS,
                    PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS);
    /* IO_BANK0_GPIOn_CTRL: write FUNCSEL into bits [4:0]. */
    io_bank0_hw->io[gpio].ctrl = fn;
}
```

The `hw_write_masked` helper expands (in `hardware/address_mapped.h`) to a write to the **XOR alias** of the register, computing the bits to flip from the current value and the desired value. This is the atomic alias pattern from Lecture 1, applied through the SDK.

If you compile a program that contains exactly `gpio_init(15);` and disassemble with `arm-none-eabi-objdump -d`, you find approximately the following Thumb-2 instructions (paraphrased):

```
   gpio_init:
       movs    r1, #1
       lsls    r1, r1, r0          ; r1 = 1 << gpio (r0 holds gpio number)
       ldr     r2, =0xd0000028     ; SIO_GPIO_OE_CLR
       str     r1, [r2]
       ldr     r2, =0xd0000018     ; SIO_GPIO_OUT_CLR
       str     r1, [r2]
       movs    r1, #5              ; FUNCSEL = SIO
       b       gpio_set_function
```

Eight instructions. ~10 cycles. The address `0xd000_0028` is `SIO_GPIO_OE_CLR`, and `0xd000_0018` is `SIO_GPIO_OUT_CLR`, both predicted from Lecture 1. The SDK is not doing magic. It is doing the same writes you would do, in the same order, with the right address constants.

---

## 3. Reading `gpio_put`

The fast path — `gpio_put(pin, value)` — is, in the SDK source:

```c
static inline void gpio_put(uint gpio, bool value) {
    uint32_t mask = 1ul << gpio;
    if (value) {
        sio_hw->gpio_set = mask;   /* SIO_GPIO_OUT_SET, offset 0x014 */
    } else {
        sio_hw->gpio_clr = mask;   /* SIO_GPIO_OUT_CLR, offset 0x018 */
    }
}
```

Note the `static inline`. When you call `gpio_put(15, 1)`, the compiler inlines this and the resulting machine code is:

```
   movs    r0, #1
   lsls    r0, r0, #15          ; r0 = 1 << 15 = 0x8000
   ldr     r1, =0xd0000014      ; SIO_GPIO_OUT_SET
   str     r0, [r1]
```

Four instructions. Three of them are constants the compiler can sometimes hoist out of a loop. The SDK call costs **zero extra cycles** vs the hand-rolled register write, when used in a context the compiler can inline. This is the same observation we will make about C++ in Week 4: **a well-designed abstraction is free**.

The reason `gpio_put` is preferable to the raw write, even when both produce the same machine code, is **readability** and **bank-agnosticism**. A future reader of your code sees the intent; a future port to RP2350 (whose pin layout differs) does not require touching every call site.

---

## 4. The atomic alias, via the SDK

`gpio_xor_mask` is one line:

```c
static inline void gpio_xor_mask(uint32_t mask) {
    sio_hw->gpio_togl = mask;   /* SIO_GPIO_OUT_XOR, offset 0x01c */
}
```

Disassembly:

```
   ldr     r1, =0xd000001c
   str     r0, [r1]
```

Two instructions. **One cycle for the store, one cycle for the constant load (or zero if the constant is already in a register from a prior iteration of a loop).** This is the "as fast as the silicon allows" toggle.

To prove the SDK matches the hand-rolled raw write to `0xd000_001c`, compile both into the same `.elf` and `objdump` it:

```c
void toggle_raw(void)    { *(volatile uint32_t *)0xd000001c = 1u << 25; }
void toggle_sdk(void)    { sio_hw->gpio_togl = 1u << 25; }
```

The `objdump -d` output for both is byte-identical (the function names differ; the body is the same three instructions: `movs`, `lsls`, `ldr` of the address, `str`). If you ever doubt whether the SDK is "really" hitting the register, dump and read.

---

## 5. Reading `uart_init`

The UART path is more complicated because the PL011 has a strict configuration order. The SDK's `uart_init` is ~40 lines; the essence is:

```c
uint uart_init(uart_inst_t *uart, uint baudrate) {
    /* 1. Bring UART out of reset. */
    reset_block(uart_get_index(uart) ? RESETS_RESET_UART1_BITS
                                     : RESETS_RESET_UART0_BITS);
    unreset_block_wait(uart_get_index(uart) ? RESETS_RESET_UART1_BITS
                                            : RESETS_RESET_UART0_BITS);
    
    /* 2. Set the baud rate (computes UARTIBRD, UARTFBRD from clk_peri). */
    uint baud = uart_set_baudrate(uart, baudrate);
    
    /* 3. Set default format: 8 data bits, no parity, 1 stop, FIFOs on. */
    uart_set_format(uart, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart, true);
    
    /* 4. Enable the UART: UARTEN, TXE, RXE. */
    hw_set_bits(&uart_get_hw(uart)->cr,
                UART_UARTCR_UARTEN_BITS
              | UART_UARTCR_TXE_BITS
              | UART_UARTCR_RXE_BITS);
    
    /* 5. Always enable DREQ signals for DMA (no-op if DMA unused). */
    hw_set_bits(&uart_get_hw(uart)->dmacr,
                UART_UARTDMACR_TXDMAE_BITS | UART_UARTDMACR_RXDMAE_BITS);
    
    return baud;
}
```

Five steps. Same five steps you wrote by hand in Lecture 2's §4. Now read `uart_set_baudrate`:

```c
uint uart_set_baudrate(uart_inst_t *uart, uint baudrate) {
    uint32_t baud_rate_div = (8 * clock_get_hz(clk_peri) / baudrate);
    uint32_t baud_ibrd = baud_rate_div >> 7;
    uint32_t baud_fbrd;
    
    if (baud_ibrd == 0) {
        baud_ibrd = 1;
        baud_fbrd = 0;
    } else if (baud_ibrd >= 65535) {
        baud_ibrd = 65535;
        baud_fbrd = 0;
    } else {
        baud_fbrd = ((baud_rate_div & 0x7f) + 1) / 2;
    }
    
    uart_get_hw(uart)->ibrd = baud_ibrd;
    uart_get_hw(uart)->fbrd = baud_fbrd;
    
    /* PL011 requires a dummy LCR write to latch the divisor. */
    hw_set_bits(&uart_get_hw(uart)->lcr_h, 0);
    
    return (4 * clock_get_hz(clk_peri)) / (64 * baud_ibrd + baud_fbrd);
}
```

A few subtleties to notice:

- The SDK computes `8 * clk_peri / baud` rather than `clk_peri / (16 * baud)`. Same math, rearranged to avoid a divide by 16 inside a 32-bit multiply. The result `>> 7` gives `IBRD` (the integer part, scaled), and `& 0x7f` gives the fractional part scaled to 7 bits, which is rounded to 6 bits with `+ 1) / 2`. This is the standard PL011 implementation; the ARM PrimeCell TRM (DDI 0183) recommends exactly this.
- Notice the **dummy LCR write at the end**. This is the latch-the-divisor write we mentioned in Lecture 2. Without it, the divisor sits in the register but does not take effect. This is the kind of detail the SDK gets right and a hand-rolled bring-up will get wrong on the first attempt.
- `clock_get_hz(clk_peri)` is the call we said you must use if you change the system clock. Hand-rolling `125_000_000` instead is the bug.

---

## 6. The cost of the SDK — measured

A common complaint about vendor SDKs is "they are bloated." Let us actually measure.

Build A: a blink program using `gpio_init`, `gpio_set_dir`, `gpio_put`. Build B: the same blink, with raw register writes inline. Both targeting Pi Pico W with `pico_stdlib`:

```
   $ arm-none-eabi-size build/blink-sdk.elf
      text    data     bss     dec     hex filename
     21344     324    8224   29892    74c4 build/blink-sdk.elf
   
   $ arm-none-eabi-size build/blink-raw.elf
      text    data     bss     dec     hex filename
     20928     292    8208   29428    72f4 build/blink-raw.elf
```

The SDK build is **416 bytes larger** (`.text`) and **32 bytes larger** (`.data`). The 416 bytes is mostly the `gpio_set_function` bank-selection logic and the `RESETS_*` helpers, both of which would have to exist in any non-trivial firmware anyway. The 32 bytes of `.data` is the SDK's runtime config tables.

**Bottom line:** the SDK costs you about 1.5 KB of flash on top of a hand-rolled program, in exchange for bank-agnosticism, type safety, and the reassurance that the PL011 latch-the-divisor write is happening. On a 2 MB flash, this is a 0.07% overhead. Pay it.

For runtime cycle cost, we measured earlier:

| Call | Cycles | Notes |
|---|---:|---|
| `*(volatile uint32_t *)0xd000_001c = 1u << 25;` | 2 | Hand-rolled XOR |
| `sio_hw->gpio_togl = 1u << 25;` | 2 | SDK struct, inlined |
| `gpio_xor_mask(1u << 25);` | 2 | SDK function, inlined |
| `gpio_put(25, !gpio_get(25));` | ~12 | Read, NOT, branch, write |

The first three are equivalent. The fourth is what an Arduino user would write (`digitalWrite(LED, !digitalRead(LED))`), and it is 6x slower. Style matters.

---

## 7. When to drop down to registers, in production

The decision rule, in priority order:

1. **The SDK does not expose the feature.** Some PL011 features (the RX timeout interrupt threshold, certain DMA arbitration tweaks) are not in the public SDK API. You have to write the register. Cite the datasheet in the comment.
2. **You are in an ISR and every cycle counts.** The SDK's helpers are designed for clarity, not for the worst-case interrupt path. For ISRs that fire every 100 µs or faster, drop to the `*_hw` struct or to direct register writes. We will see this in Week 11 (DMA + audio).
3. **You are writing a custom peripheral driver and want to read like the datasheet.** The `*_hw` struct path (e.g., `uart_get_hw(uart0)->ibrd = 67;`) reads register-name-for-register-name. This is the C7-preferred middle ground.
4. **You are debugging and need to know exactly what the silicon sees.** Drop to `volatile uint32_t *` writes temporarily, capture on a logic analyzer, then restore the SDK call once you have proven the silicon path.

The decision rule, restated:

> **Default to the SDK. Drop one level (to `_hw` structs) when readability demands it. Drop two levels (to raw register writes) only with a comment citing why.**

The mini-project this week asks for **one** raw register write. Choose one where the comment writes itself: maybe the atomic `SIO_GPIO_OUT_XOR` write inside the blink loop, with a comment saying "this is the hot path; SDK call adds ~10 cycles per toggle."

---

## 8. The bank-agnosticism payoff — RP2350 preview

The follow-on chip to the RP2040, the RP2350 (announced August 2024), keeps the SIO at `0xd000_0000` but doubles the GPIO count to 48 and adds a second pad bank. The SDK's `gpio_init`, `gpio_put`, `gpio_xor_mask` all work unchanged on the RP2350 because the SDK abstracts the bank selection. Code that wrote `*(volatile uint32_t *)0xd000_001c = 1u << 47;` on the RP2350 still toggles GP15 (the lowest pad) but to use GP47 you must now write to a *different* SIO register that the RP2040 did not have.

In other words: the same source code that called `gpio_xor_mask(1u << 47)` will compile and run correctly against the RP2350 SDK. The hand-rolled register-address constant will not.

This is not a hypothetical. By Week 8 or 9, half of you will be experimenting on RP2350 boards because they will be cheaper than the RP2040 by then. Write the SDK call.

---

## 9. Three concrete predictions to confirm at the bench

Verify each on your kit this week:

1. **Disassemble** Exercise 1's raw register toggle and Exercise 2's SDK `uart_putc`. Compare them to the lecture snippets. Find the `STR` and `LDR` instructions; confirm the addresses match.
2. **Measure** the LED toggle rate with `gpio_xor_mask(1u << 25)` in a tight `while(1)` loop. Expected: ~25 MHz at 125 MHz clock (the loop overhead dominates). The SDK call does not slow you down.
3. **Run** Exercise 2's UART echo with the SDK's `uart_init(uart0, 115200)`. Measure the bit period on a Saleae. Confirm `8.681 µs ± 1%`. The SDK gets the divisor right; trust it.

If any of the three does not match, you have a bug in your setup, not in the SDK. The SDK is one of the most-tested code bases in embedded; it gets these right. The first move when something does not match is to suspect your wiring, your clock config, or your terminal — not the SDK.

---

## 10. Up next

This is the last lecture of Week 2. Tomorrow's bench time is the mini-project: a firmware that produces a UART log over UART0 and blinks the on-board LED at a button-controlled rate, with at least one direct register write somewhere in the codebase and a `REGISTER-TABLE.md` documenting every register touched.

Before you start the mini-project, re-read Lectures 1 and 2's "what to do this week" sections. The sticky-note addresses (eight in total — four SIO, four UART) are the entire surface area of your register table for this week.

By Week 4 (C++ templates), Week 5 (Rust `embedded-hal`), and Week 9 (FreeRTOS queues), you will have moved several abstraction levels above the SDK. The exercises here will feel quaint. They are still the foundation; every layer above eventually compiles down to a write to `SIO_GPIO_OUT_XOR`.

Trust the abstractions. Verify them once. Then ship.
