# Challenge 1 — UART Hello World

**Time estimate:** ~150 minutes (mostly wiring and bench setup; coding is ~20 min).

## Problem statement

Make a Pi Pico W transmit `"crunch-wire week-01 boot ok\r\n"` over **UART0** at **115200 baud, 8N1**, once per second. Wire the Pico's TX line to a USB-serial bridge plugged into your laptop, and view the message scrolling in a terminal. Then capture the *bytes themselves* on a logic analyzer or oscilloscope, decode them by hand from the timing, and confirm they match what the terminal printed.

This is the second piece of the bring-up note. By the end you will own the UART layer of every later week's debug story.

## Acceptance criteria

- [ ] A C program on the Pi Pico W transmits the literal string `crunch-wire week-01 boot ok\r\n` over UART0 once per second.
- [ ] UART0 is configured on the default pins **GP0 = TX, GP1 = RX** at **115200 baud, 8 data bits, no parity, 1 stop bit**.
- [ ] You have wired GP0 (TX), GND, and (if your bridge needs it) `3V3(OUT)` to a USB-serial bridge (CP2102, CH340, FT232, or an FTDI cable).
- [ ] On your laptop, `screen`, `picocom`, `minicom`, or `mpremote` shows the string scrolling once per second at 115200 8N1.
- [ ] You captured a logic-analyzer trace (Saleae, `sigrok`/PulseView, or a cheap 8-channel clone) of the GP0 line for at least one full message, **decoded by the analyzer** as "Async Serial 115200 8N1," and confirmed character-by-character that the decoded ASCII matches the source string.
- [ ] You measured the **actual bit period** on the trace and confirmed it is **8.68 µs ± 1%** (1 / 115200 s).
- [ ] A `notes/uart-hello.md` file in your Week 1 repo includes the source, the terminal screenshot, the logic-analyzer screenshot with decoded characters visible, and a one-paragraph reflection on what surprised you.

## Hints

<details>
<summary>Wiring (USB-serial bridge → Pi Pico W)</summary>

A CP2102 / CH340 / FT232 USB-serial dongle has these pins (almost universally):

```
   Bridge   Pi Pico W       Notes
   ──────   ─────────       ─────
   TXD ──── GP1  (RX)       bridge → Pico
   RXD ──── GP0  (TX)       Pico → bridge
   GND ──── GND             common ground (mandatory, not optional)
   3V3 ──── 3V3(OUT)        if you want bridge to power Pico (skip if Pico is on USB)
   5V       — DO NOT WIRE   most bridges output 5 V on this; the Pico GPIO is 3.3 V tolerant only
```

If your dongle has only 5V (no 3V3 out), power the Pico from USB and wire only TXD/RXD/GND. Never put 5 V on a GPIO; it dies, at typical max 3.63 V on V_IO.

Order of plug-in: **GND first**, then signals, then power. This is bench hygiene; flipped, you can briefly inject current into a GPIO without a clean ground reference.

</details>

<details>
<summary>The C source (`uart_hello.c`)</summary>

```c
/* C7 · Crunch Wire — Week 01 — UART hello world.
 *
 * Hardware: Pi Pico W
 * UART:     UART0 on GP0 (TX) and GP1 (RX), 115200 8N1
 * SDK:      pico-sdk >= 1.5.1
 *
 * Build (CMakeLists.txt: target_link_libraries(uart_hello pico_stdlib))
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

#define UART_ID    uart0
#define BAUD_RATE  115200
#define UART_TX_PIN  0   /* GP0 */
#define UART_RX_PIN  1   /* GP1 */

int main(void)
{
    /* Bring up UART0 at 115200, 8N1.  uart_init returns the *actual* baud
     * achieved after PLL/divider rounding; for 125 MHz clk_peri and 115200
     * target, this is 115107 — a 0.08% deviation, well inside the ±2.5%
     * Async Serial tolerance.  RP2040 datasheet §4.2.7. */
    (void)uart_init(UART_ID, BAUD_RATE);

    /* Mux GP0/GP1 into UART0 function (AF F2).  See RP2040 datasheet
     * §2.19.6.1, p. 244, GPIO function-select table. */
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    /* 8 data bits, no parity, 1 stop bit.  These are the SDK defaults but
     * we state them explicitly so the wire format is in the source. */
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);

    /* No HW flow control on the Pico W carrier — the RTS/CTS pins are not
     * exposed on default UART0 mapping. */
    uart_set_hw_flow(UART_ID, false, false);

    /* Disable the FIFO so we observe one byte per write on the scope.
     * For a real driver, leave this on; we turn it off only to keep the
     * Saleae decoding clean for this challenge. */
    uart_set_fifo_enabled(UART_ID, false);

    for (;;) {
        uart_puts(UART_ID, "crunch-wire week-01 boot ok\r\n");
        sleep_ms(1000);
    }
}
```

</details>

<details>
<summary>CMakeLists.txt diffs from Exercise 2</summary>

```cmake
add_executable(uart_hello uart_hello.c)

target_link_libraries(uart_hello
    pico_stdlib
    hardware_uart        # <— add this
)

pico_add_extra_outputs(uart_hello)
```

You do **not** need `pico_cyw43_arch_none` for this challenge (no LED).

</details>

<details>
<summary>Watching the terminal output</summary>

The USB-serial bridge enumerates as `/dev/tty.usbserial-XXXX` (macOS) or `/dev/ttyUSB0` (Linux). Open it:

```bash
# Option A: screen
screen /dev/ttyUSB0 115200
# Exit: Ctrl-A then K, then y.

# Option B: picocom (friendlier; install via apt or brew)
picocom -b 115200 /dev/ttyUSB0
# Exit: Ctrl-A Ctrl-X

# Option C: minicom
minicom -D /dev/ttyUSB0 -b 115200
```

You should see:

```
crunch-wire week-01 boot ok
crunch-wire week-01 boot ok
crunch-wire week-01 boot ok
…
```

If characters are garbled but consistent, you have a baud mismatch. Confirm the host opened the port at 115200. If characters are missing, you may have the TX/RX backwards or a bad ground.

</details>

<details>
<summary>Capturing on a logic analyzer</summary>

1. Connect the Saleae (or `sigrok` / PulseView clone) channel 0 to GP0 on the Pico, and the Saleae GND to the Pico GND.
2. In Logic 2 (or PulseView), set the sample rate to **at least 2 MHz** (≥ 20× the bit rate is conservative). 4 MHz is fine.
3. Set the capture duration to ~3 seconds — enough for two full messages.
4. Start the capture, observe at least two `crunch-wire week-01 boot ok` strings come through, stop.
5. In Logic 2: right-click the channel → "Add Analyzer" → "Async Serial" → 115200 baud, 8 data bits, 1 stop bit, no parity. The decoded ASCII appears above the waveform.
6. Zoom in on one byte. Confirm 10 bits visible: 1 start (low), 8 data (LSB first), 1 stop (high). Measure the bit period: ~8.68 µs at 115200 baud.

Saleae export: File → Save → captures as `.sal` (Logic 2) or `.logicdata` (Logic 1). Commit the file to your repo (the `.sal` is small; tens of KB for this trace).

</details>

<details>
<summary>Decoding by hand — the math</summary>

For the character `'c'` (ASCII 0x63 = `0b01100011`):

```
   line:    │‾‾‾│___│‾‾│___│___│‾‾│‾‾│___│___│‾‾‾│
   bit  :    idle st  d0  d1  d2  d3  d4  d5  d6  d7   sp
   value:    1   0   1   1   0   0   0   1   1   0    1
   meaning: idle start LSB                      MSB  stop

   note: data bits sent LSB first.  0x63 = 0b01100011
         re-read LSB first  →     1 1 0 0 0 1 1 0
         that matches the line above between start and stop.
```

This is the calibration moment. The serial line is *just* a clocked sequence of voltage levels, and any "framing" — start bit, stop bit, parity, FIFO depth — is software convention layered on top. You decoded one byte by hand. You can now decode any UART trace.

</details>

<details>
<summary>If the terminal shows nothing</summary>

In order of likelihood:

1. **TX/RX swapped.** The dongle's TX must go to the Pico's RX (GP1), and vice versa. Switch the two signal wires.
2. **Missing ground.** Common GND is mandatory. The signals are referenced to it.
3. **Wrong baud.** Host opened at 9600 by default. Confirm 115200.
4. **Pico is in BOOTSEL.** It has not run your firmware. Replug it without holding BOOTSEL.
5. **`PICO_BOARD` not set.** Some default board configurations remap UART0 to alternate pins. Add `-DPICO_BOARD=pico_w` to your `cmake` invocation.

</details>

## Stretch

- Replace `uart_puts` with a hand-rolled `for` loop that pokes `UART0_DR` directly (datasheet §4.2.5, UART0_BASE = 0x4003_4000, DR offset 0x000). This is a Week-3 preview of register-level UART access.
- Add a parity bit (even). Reconfigure the bridge to match. Verify the Saleae decoder still works.
- Push baud to 921600. At what point does the message start to corrupt? Plot a curve.
- Wire a second UART (UART1 on GP4/GP5) and echo received bytes back. Test it with a single FTDI cable looped from your laptop to UART0 in and UART1 out.

## Why this matters

UART is the lowest-overhead, lowest-software-cost debug channel an embedded engineer has. Every embedded product you will ever ship has a UART somewhere, even if it is unpopulated headers on production boards. The skill being built here — wire it, send bytes, *prove they came through with a logic analyzer*, debug a baud mismatch in 30 seconds — is a skill you will use in Week 2 and in Week 24. It is the closest thing embedded engineering has to a universal solvent.

Reviewers will also use this challenge as a proxy for bench hygiene: did you label your screenshots, did you commit the `.sal` file, did you cite the datasheet section for the function-select bits? Week 1's voice rules apply.

## Submission

Commit `notes/uart-hello.md` containing:

- The source (or a link to it in your Week 1 repo).
- The terminal screenshot.
- The Saleae / PulseView screenshot with decoded ASCII visible.
- The measured bit period (in µs) and the deviation from the 8.68 µs ideal.
- A one-paragraph reflection: what surprised you?

This file feeds directly into the [bring-up note mini-project](../mini-project/README.md).
