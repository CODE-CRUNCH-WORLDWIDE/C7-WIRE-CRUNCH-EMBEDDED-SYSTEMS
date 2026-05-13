# Exercise 2 — UART Echo

**Time estimate:** ~120 minutes (~30 for wiring, ~60 for code + verification, ~30 for the Saleae deep-dive).

## Problem statement

Build a C firmware for the Pi Pico W that brings up UART0 at 115200 8N1 and echoes every received byte back out, indefinitely. Wire UART0 (GP0 = TX, GP1 = RX) to a USB-serial bridge plugged into your laptop. Type characters in a terminal; see them come back. Then capture both the TX and RX lines on a logic analyzer and verify the bit period is `8.681 µs ± 1%`.

You will write the bring-up using the `pico-sdk` `uart_init()` call (which is what you would ship), and you will also produce a second build where the divisor is computed and written **by hand** to `UARTIBRD` and `UARTFBRD` to prove you understand the math.

## Acceptance criteria

- [ ] A new directory `c7-week02-uart-echo/` with `echo.c`, `CMakeLists.txt`, and `pico_sdk_import.cmake`.
- [ ] `cmake -B build -DPICO_BOARD=pico_w . && cmake --build build -j` succeeds with no warnings.
- [ ] UART0 is configured at **115200 baud, 8 data bits, no parity, 1 stop bit, FIFOs enabled**.
- [ ] GP0 is UART0 TX, GP1 is UART0 RX (the default mux).
- [ ] You have wired GP0 (TX) → bridge RX, GP1 (RX) ← bridge TX, GND → bridge GND, to a CP2102 / CH340 / FT232 USB-serial dongle.
- [ ] In a serial terminal (`screen /dev/cu.usbserial-* 115200`, `picocom -b 115200 /dev/ttyUSB0`, or equivalent) you can type characters and see them echoed back.
- [ ] You produced **two builds**: `echo-sdk.elf` (uses `uart_init`) and `echo-raw.elf` (writes `UARTIBRD = 67`, `UARTFBRD = 52` directly). Both work identically on the bench.
- [ ] You captured a Saleae or PulseView trace of GP0 (TX) with the "Async Serial 115200 8N1" decoder applied, decoding at least the bytes `crunch-wire\r\n` round-tripped through your firmware. Bit period measured on the trace is `8.681 µs ± 1%`.
- [ ] A `notes/uart-echo.md` documents the divisor computation, the two builds, the Saleae screenshot, and any bugs you hit.

## Hints

<details>
<summary>The C source (`echo.c`) — SDK version</summary>

```c
/* C7 · Crunch Wire — Week 02 — UART0 echo at 115200 8N1.
 *
 * Hardware: Raspberry Pi Pico W
 * UART:     UART0, GP0 (TX) / GP1 (RX), 115200 8N1
 * SDK:      pico-sdk >= 1.5.1
 *
 * Build:
 *   cmake -B build -DPICO_BOARD=pico_w .
 *   cmake --build build -j
 * Flash:
 *   drag build/echo-sdk.uf2 to RPI-RP2 in BOOTSEL mode
 */

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#define UART_ID    uart0
#define BAUD_RATE  115200
#define UART_TX    0
#define UART_RX    1

int main(void) {
    /* 1. Initialize UART0 at 115200 baud, default 8N1, FIFOs on. */
    uart_init(UART_ID, BAUD_RATE);

    /* 2. Route GP0/GP1 to UART0 (FUNCSEL = 2 = UART). */
    gpio_set_function(UART_TX, GPIO_FUNC_UART);
    gpio_set_function(UART_RX, GPIO_FUNC_UART);

    /* 3. Send a boot banner so a terminal user sees a sign of life. */
    uart_puts(UART_ID, "crunch-wire w02 echo ready\r\n");

    /* 4. Echo loop. */
    while (1) {
        if (uart_is_readable(UART_ID)) {
            char c = uart_getc(UART_ID);
            uart_putc(UART_ID, c);
        }
    }
    return 0;
}
```

</details>

<details>
<summary>The C source — raw-divisor version (`echo-raw.c`)</summary>

```c
/* C7 · Crunch Wire — Week 02 — UART0 echo, divisor written by hand.
 *
 * Computes UARTIBRD = 67, UARTFBRD = 52 from:
 *   BAUDDIV = 125_000_000 / (16 × 115_200) = 67.8168
 *   IBRD = floor(67.8168) = 67
 *   FBRD = round(0.8168 × 64) = 52
 * Actual baud: 115_181.32 baud, error -0.016% (well inside PL011 2% tolerance).
 *
 * Registers used (citations to RP2040 datasheet, Sep-2024 rev):
 *   UART0_IBRD   0x4003_4024  (§4.2.7.4, p. 435)
 *   UART0_FBRD   0x4003_4028  (§4.2.7.5, p. 435)
 *   UART0_LCR_H  0x4003_402c  (§4.2.7.6, p. 436)
 *   UART0_CR     0x4003_4030  (§4.2.7.7, p. 437)
 *   UART0_DR     0x4003_4000  (§4.2.7.1, p. 432)
 *   UART0_FR     0x4003_4018  (§4.2.7.3, p. 434)
 *   RESETS_RESET_CLR  0x4000_f000  (§2.14, p. 215)
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define UART0_BASE   0x40034000u
#define UART0_DR     (*(volatile uint32_t *)(UART0_BASE + 0x000u))
#define UART0_FR     (*(volatile uint32_t *)(UART0_BASE + 0x018u))
#define UART0_IBRD   (*(volatile uint32_t *)(UART0_BASE + 0x024u))
#define UART0_FBRD   (*(volatile uint32_t *)(UART0_BASE + 0x028u))
#define UART0_LCR_H  (*(volatile uint32_t *)(UART0_BASE + 0x02cu))
#define UART0_CR     (*(volatile uint32_t *)(UART0_BASE + 0x030u))

#define RESETS_BASE         0x4000c000u
#define RESETS_RESET_CLR    (*(volatile uint32_t *)(RESETS_BASE + 0x3000u))
#define RESETS_DONE         (*(volatile uint32_t *)(RESETS_BASE + 0x008u))

#define UART0_RESET_BIT     (1u << 22)

static void uart0_init_raw(void) {
    /* 1. Deassert UART0 reset and wait for RESETS_DONE bit 22 = 1. */
    RESETS_RESET_CLR = UART0_RESET_BIT;
    while ((RESETS_DONE & UART0_RESET_BIT) == 0) { /* spin */ }

    /* 2. Disable UART before configuring. */
    UART0_CR = 0u;

    /* 3. Program divisor. 125 MHz / (16 × 115200) = 67.8168. */
    UART0_IBRD = 67u;
    UART0_FBRD = 52u;

    /* 4. Set format: 8 bits, FIFOs enabled. WLEN=3 in [6:5], FEN bit 4. */
    UART0_LCR_H = (3u << 5) | (1u << 4);

    /* 5. Enable UART: UARTEN bit 0, TXE bit 8, RXE bit 9. */
    UART0_CR = (1u << 0) | (1u << 8) | (1u << 9);
}

static void uart0_putc_raw(char c) {
    while (UART0_FR & (1u << 5)) { /* TXFF set — TX FIFO full */ }
    UART0_DR = (uint32_t)c;
}

static int uart0_getc_nonblock_raw(void) {
    if (UART0_FR & (1u << 4)) return -1;  /* RXFE — RX FIFO empty */
    return (int)(UART0_DR & 0xffu);
}

static void uart0_puts_raw(const char *s) {
    while (*s) uart0_putc_raw(*s++);
}

int main(void) {
    /* Route GP0 / GP1 to UART0 — we still use the SDK for the mux. */
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);

    uart0_init_raw();

    uart0_puts_raw("crunch-wire w02 echo-raw ready\r\n");

    while (1) {
        int c = uart0_getc_nonblock_raw();
        if (c >= 0) uart0_putc_raw((char)c);
    }
    return 0;
}
```

</details>

<details>
<summary>Wiring</summary>

```
   Bridge (CP2102 / CH340 / FT232)    Pi Pico W
   ───────────────────────────────    ──────────────
   TXD                ────────────►   GP1 (UART0 RX)
   RXD                ◄────────────   GP0 (UART0 TX)
   GND                ─── common ──   GND  (mandatory)
   3V3 (optional)     ───── 3.3V ──   3V3(OUT)  (skip if Pico is on USB)
   5V                 — DO NOT WIRE
```

Connect GND **first**. Never wire 5 V to a GPIO; the Pico's V_IO max is 3.63 V (datasheet §5.2).

</details>

<details>
<summary>Verifying the bit period on a Saleae</summary>

1. Connect Saleae channel 0 to GP0 (Pico TX → bridge RX) and channel 1 to GP1 (bridge TX → Pico RX). Common ground to GND.
2. Set sample rate to **4 MHz** (this gives you ~35 samples per bit at 115200 baud — plenty of headroom).
3. Capture for ~2 seconds while typing `crunch-wire\r\n` in your terminal.
4. Apply the **Async Serial** decoder: bit rate 115200, 8 data bits, 1 stop, no parity, LSB-first.
5. Zoom in on a single character. Measure the bit period with the cursor tool — measure from the *falling edge* of the start bit to the *falling edge* of bit-7 → stop transition (or the next clean edge). Divide by 9 to get the bit period.

Expected: **8.68 µs ± 0.05 µs**. Anything outside `8.59 µs` to `8.77 µs` means your divisor is off, or your peripheral clock is not 125 MHz. The SDK build should land at 8.68 µs exactly; the raw build (with IBRD=67, FBRD=52) should land at 8.683 µs (the 0.016% error from §3 of Lecture 2, computed: `1 / 115_181.32 = 8.6820 µs`).

</details>

## What to capture

In `notes/uart-echo.md`, the structure:

```
# Exercise 2 — UART Echo at 115200 8N1

## Divisor computation (clk_peri = 125 MHz, baud = 115200)

  BAUDDIV  = 125_000_000 / (16 × 115_200) = 67.8168
  IBRD     = floor(67.8168) = 67
  FBRD     = round(0.8168 × 64) = 52
  Actual   = 125_000_000 / (16 × (67 + 52/64)) = 115_181.32 baud
  Error    = -0.016%

## Registers touched (raw build)

| Register | Address | Value written | Datasheet § / page |
|---|---:|---:|---|
| RESETS_RESET_CLR | 0x4000_f000 | 0x0040_0000 (bit 22) | §2.14, p. 215 |
| UART0_CR | 0x4003_4030 | 0x0 then 0x301 | §4.2.7.7, p. 437 |
| UART0_IBRD | 0x4003_4024 | 67 | §4.2.7.4, p. 435 |
| UART0_FBRD | 0x4003_4028 | 52 | §4.2.7.5, p. 435 |
| UART0_LCR_H | 0x4003_402c | 0x70 (8 bits, FIFO on) | §4.2.7.6, p. 436 |

## Saleae trace

[Screenshot: GP0 + GP1, ~5 characters round-tripped, Async Serial decoder visible]
Measured bit period: 8.68 µs (cursor measurement)
Decoded ASCII: matches typed input

## Bugs hit

  - Bug 1: forgot to add `pico_enable_stdio_uart(echo-sdk 0)` and the
    default printf went out UART0 alongside the echo, doubling every line.
    Fix: disable stdio_uart on UART0.
  - Bug 2: forgot common ground; terminal showed random bytes.
    Fix: GND wire first. Always.

## Reflection

Two paragraphs:
1. Did the SDK build and the raw build show identical bit periods?
2. What is the smallest baud rate change that the PL011 divisor cannot
   produce exactly with a 125 MHz clk_peri? (Hint: any baud where
   125_000_000 / (16 × baud) has a 7th decimal that is not 0.)
```

## Stretch goals

- Push the baud rate to **921600**. Recompute the divisor (it works out to ~8.47, IBRD=8, FBRD=30). Confirm the bit period is `1.085 µs ± 1%` on the Saleae. Note how much closer you are to the PL011's tolerance margin.
- Implement a polled receive that buffers a full line (terminated by `\n` or `\r`) before echoing it back. You will hit the FIFO-overrun bug if you type fast; document the fix.
- Compare the binary size of `echo-sdk.elf` and `echo-raw.elf` with `arm-none-eabi-size`. Which is smaller? Why?
- Run the echo at 115200 8E1 (even parity, 1 stop). This requires setting `PEN` (bit 1) and `EPS` (bit 2) in `UARTLCR_H`. Verify your terminal also speaks 8E1; otherwise the bytes look like garbage on the screen.

## Why this matters

UART is the universal embedded debug channel. Every microcontroller you ever ship will have one of these, and the first thing you do on a new board is bring up the UART and print "hello." If you cannot do this from registers in ~30 minutes by Week 12, you will lose hours every time you bring up a new chip family.

The bit-period measurement on the logic analyzer is the load-bearing skill. Once you have seen 8.68 µs ± 1% with your own eyes, the PL011 divisor math stops feeling magical and starts feeling like arithmetic.
