# Lecture 2 — UART Bit Timing and Handshake

> **Outcome:** You can compute the `UARTIBRD` and `UARTFBRD` divisor values that put the RP2040 UART0 at exactly 115200 baud from a 125 MHz peripheral clock, and you can read a Saleae capture of a single character on the wire and tell, from the bit period alone, whether the divisor is correct within the PL011's 1% tolerance. You can draw the eleven cells of a single 8N1 character on a napkin and label each one.

---

## 1. What a UART actually is

A Universal Asynchronous Receiver/Transmitter is the simplest peripheral on any MCU that can move a byte off the chip. It has three jobs:

1. **Serialize bytes onto a wire.** Take 8 bits from a register, shift them out LSB-first at a fixed clock rate, surrounded by a start bit (low) and a stop bit (high). The "asynchronous" half of "UART" means there is no separate clock line; the receiver recovers timing from the start-bit transition alone.
2. **De-serialize bytes off a wire.** Reverse the above. Sample the line at 16x the bit rate (the PL011's default), detect the start-bit transition, then sample each of the 8 data bits in the middle of its bit cell, then verify the stop bit is high.
3. **Buffer in both directions.** Most production UART blocks include a FIFO so that the CPU does not have to service every single byte at the bit rate. The RP2040 PL011 has a 32-byte TX FIFO and a 32-byte RX FIFO (datasheet §4.2.1, p. 425).

That is the entire job description. Every UART chip since the 8250 in 1980 has done these three things and not much else. The ARM PL011 PrimeCell is the modern descendant; the RP2040 datasheet §4.2 cites the ARM PL011 TRM (ARM DDI 0183) as the authoritative source for register semantics.

This week we will care about exactly one piece of the PL011: the baud-rate divisor. The rest is reading the datasheet.

---

## 2. The character on the wire

A single ASCII character at **115200 baud, 8 data bits, no parity, 1 stop bit (8N1)** looks like this on a logic analyzer, idle-high (the convention every UART since 1980 has used):

```
    Idle    Start    bit0    bit1    bit2    bit3    bit4    bit5    bit6    bit7    Stop    Idle
   _____   |       |                                                              |       |_____
   3.3V    |       |       |       |       |       |       |       |       |     |       3.3V
          ↓                                                                       ↑
        0 V                                                                      3.3V
        
   <----><------><------><------><------><------><------><------><------><------><------>
   idle  8.68µs  8.68µs  8.68µs  8.68µs  8.68µs  8.68µs  8.68µs  8.68µs  8.68µs  8.68µs
         (start) (LSB)                                                  (MSB)   (stop)
```

Eleven bit cells in total: idle-high, then **one start bit at logic 0**, then **eight data bits LSB-first**, then **one stop bit at logic 1**, then idle-high again. The bit period at 115200 baud is **1 / 115200 s = 8.681 µs**.

A single character takes **10 bit periods × 8.681 µs = 86.81 µs** to transmit (the idle time before and after is not part of the character). The maximum sustained character rate is therefore `115200 / 10 = 11520` characters per second, or about **11.5 KB/s** when you account for the start/stop overhead.

Three things worth pinning down right now:

- **The wire is idle-high, not idle-low.** A line stuck at 0 V is interpreted as a permanent start bit; a line stuck at 3.3 V is interpreted as idle. If your terminal shows a stream of `\0` characters, your TX line is shorted to ground.
- **Data goes out LSB-first.** This is a confusing but universal convention. The byte `0x41` ('A' = `0100 0001`) goes onto the wire as: start, **1, 0, 0, 0, 0, 0, 1, 0**, stop. The Saleae "Async Serial" decoder reverses this for you and prints `A`.
- **The stop bit is high.** This guarantees there is a falling edge before the next character's start bit, no matter what the data bits were. Without a stop bit, two adjacent 0xFF bytes would look like one continuous stream of idle.

The full Saleae trace for the string `"A"` at 115200 8N1 looks like (in the decoded view): `[start][A][stop]`, ~87 µs wide, total. We will capture this in Exercise 2.

---

## 3. The PL011 baud-rate divisor

The PL011 generates its bit clock by dividing the peripheral clock (`clk_peri`, defaulting to 125 MHz on the RP2040, sourced from the system PLL) by a **16-bit integer + 6-bit fractional** divisor. The 6-bit fractional part is the secret to hitting non-nice baud rates accurately.

From RP2040 datasheet §4.2.7.4 (p. 435) and §4.2.7.5 (p. 435), and the formula on p. 432:

```
   BAUDDIV = clk_peri / (16 × baud)

   UARTIBRD = floor(BAUDDIV)
   UARTFBRD = round((BAUDDIV - UARTIBRD) × 64)
```

The factor of 16 comes from the PL011's 16x oversampling receiver — every received bit is sampled 16 times, and the middle three samples vote on the bit value. This gives the receiver about ±2% of clock-drift tolerance, which is the whole point of the fractional divisor existing.

### Worked example — 115200 baud at 125 MHz

```
   BAUDDIV  = 125_000_000 / (16 × 115_200)
            = 125_000_000 / 1_843_200
            = 67.8168402777…

   UARTIBRD = floor(67.8168…)        = 67
   UARTFBRD = round(0.8168… × 64)    = round(52.27…) = 52
```

So you write `67` to `UARTIBRD` (`UART0_BASE + 0x024`) and `52` to `UARTFBRD` (`UART0_BASE + 0x028`). The actual baud rate produced is:

```
   actual = 125_000_000 / (16 × (67 + 52/64))
          = 125_000_000 / 1_843_500
          = 115_181.32 baud

   error  = (115_181.32 - 115_200) / 115_200 = -0.0163%
```

You are within **0.02% of the target**. The PL011's 16x oversampler will tolerate ±2% before the start-bit center-sample misses the actual mid-bit. You will measure this on a Saleae in Exercise 2 and confirm a bit period of `8.682 µs ± 0.01 µs`, indistinguishable from the 8.681 µs target on a 4 MHz sample rate.

### Why this matters

The single most common UART bug a Week-2 student writes is: **forgetting that `clk_peri` is not always 125 MHz.** If you change the system clock (e.g., for a low-power profile), or if you select a different PLL source, `clk_peri` changes and the divisor you computed is wrong by the same ratio. The SDK's `uart_set_baudrate()` reads `clock_get_hz(clk_peri)` and recomputes the divisor; if you hand-roll the divisor, you must do the same.

The second most common bug is **forgetting `UARTFBRD`**. If you write only `UARTIBRD = 68` (rounding up) you get:

```
   actual = 125_000_000 / (16 × 68) = 114_889.71 baud
   error  = (114_889.71 - 115_200) / 115_200 = -0.269%
```

This still works (-0.27% is well inside the 2% margin) but is sloppy. The PL011 has a 6-bit fractional divisor for a reason; use it.

The third most common bug is **setting the line-control register (`UARTLCR_H`) wrong**. The PL011 requires you to write `UARTLCR_H` *after* `UARTIBRD` and `UARTFBRD`, because the PL011 latches the divisor on the LCR write. Writing them in the wrong order produces "everything looks right in software but the wire shows the previous baud rate." See §4.2.7.6, p. 436.

---

## 4. The minimum UART init, by register

Here is the full register-level sequence to bring up UART0 at 115200 8N1 with the TX FIFO enabled, with no IRQs and no flow control. From RP2040 datasheet §4.2:

```c
#define UART0_BASE      0x40034000u
#define UART0_DR        (*(volatile uint32_t *)(UART0_BASE + 0x000u))   /* data        */
#define UART0_FR        (*(volatile uint32_t *)(UART0_BASE + 0x018u))   /* flag        */
#define UART0_IBRD      (*(volatile uint32_t *)(UART0_BASE + 0x024u))   /* int divisor */
#define UART0_FBRD      (*(volatile uint32_t *)(UART0_BASE + 0x028u))   /* frac divisor*/
#define UART0_LCR_H     (*(volatile uint32_t *)(UART0_BASE + 0x02cu))   /* line ctl    */
#define UART0_CR        (*(volatile uint32_t *)(UART0_BASE + 0x030u))   /* control     */

/* Resets register (RESETS at 0x4000c000) — must be deasserted before UART0
 * is even visible. SDK does this; we do it explicitly to be honest. */
#define RESETS_RESET_CLR  (*(volatile uint32_t *)(0x4000c000u + 0x3000u))

static void uart0_115200_8n1(void) {
    /* 1. Deassert UART0 reset. RESETS_RESET bit 22 = UART0. */
    RESETS_RESET_CLR = 1u << 22;
    /* Wait for RESETS_DONE bit 22 to read 1. Omitted for brevity. */

    /* 2. Set GP0 as UART0 TX. FUNCSEL = 2 (F2 = UART0). */
    *(volatile uint32_t *)(0x40014000u + 0x004u + 0u * 0x008u) = 2u;
    /* 3. Set GP1 as UART0 RX. */
    *(volatile uint32_t *)(0x40014000u + 0x004u + 1u * 0x008u) = 2u;

    /* 4. Disable UART before reconfiguring (datasheet §4.2.4). */
    UART0_CR = 0u;

    /* 5. Program the baud-rate divisor.
     *    125 MHz / (16 × 115200) = 67.8168…
     *    UARTIBRD = 67, UARTFBRD = round(0.8168×64) = 52. */
    UART0_IBRD = 67u;
    UART0_FBRD = 52u;

    /* 6. Program line control: 8 bits, no parity, 1 stop, FIFOs enabled.
     *    WLEN=3 in [6:5] → 8-bit; FEN bit 4 → FIFOs on; PEN bit 1 → parity off (0). */
    UART0_LCR_H = (3u << 5) | (1u << 4);

    /* 7. Enable the UART: UARTEN bit 0, TXE bit 8, RXE bit 9. */
    UART0_CR = (1u << 0) | (1u << 8) | (1u << 9);
}
```

The order matters. Steps 1, 4, 5, 6, 7 must be in that order; the PL011 will silently use stale divisor or LCR values otherwise. The SDK does all of this for you in `uart_init()`; we will read that function in Lecture 3 and find these exact steps.

To send a single byte:

```c
static void uart0_putc(char c) {
    /* Wait while the TX FIFO is full. UARTFR bit 5 = TXFF. */
    while (UART0_FR & (1u << 5)) { /* spin */ }
    UART0_DR = (uint32_t)c;
}
```

To receive a single byte (polled):

```c
static int uart0_getc_nonblock(void) {
    /* UARTFR bit 4 = RXFE (RX FIFO empty). */
    if (UART0_FR & (1u << 4)) return -1;
    return (int)(UART0_DR & 0xffu);
}
```

This is the entire UART API for Exercise 2. It is ~30 lines of C and three datasheet pages. Production code adds IRQs (for non-blocking RX) and DMA (for sustained TX at 921600+), which we cover in Week 7. For now: polled, 115200, 8N1.

---

## 5. The UART status registers — what to actually watch

The PL011 exposes its state through one big register: `UARTFR` (the Flag Register) at offset `0x018`. From RP2040 datasheet §4.2.7.3, p. 434:

```
   UARTFR (offset 0x018)
   
   Bit  Field   R/W  Description
   [7]  TXFE    RO   TX FIFO empty (1 = no bytes pending; safe to power down TX)
   [6]  RXFF    RO   RX FIFO full
   [5]  TXFF    RO   TX FIFO full (block here before writing UARTDR)
   [4]  RXFE    RO   RX FIFO empty (read UARTDR returns garbage if set)
   [3]  BUSY    RO   UART busy (TX still shifting a byte; do not disable TXE)
   [2]  DCD     RO   Data carrier detect (modem line; unused on RP2040 GPIO mux)
   [1]  DSR     RO   Data set ready (modem line; unused)
   [0]  CTS     RO   Clear to send (flow control; used if CTSEN is set in UARTCR)
```

The three you care about every day: `TXFF` (don't write if set), `RXFE` (don't read if set), and `BUSY` (don't power down until cleared).

The four IRQ sources, when you graduate to interrupt-driven I/O in Week 7, live in `UARTIMSC` (Interrupt Mask Set/Clear, offset `0x038`):

- **RXIM** — RX FIFO has crossed the configured threshold (default: ½ full).
- **TXIM** — TX FIFO has dropped below the configured threshold.
- **RTIM** — RX timeout: 32 bit-periods have elapsed since the last RX byte and the FIFO is non-empty. This is the "flush a partial line" interrupt.
- **FEIM** — Framing error: a stop bit was sampled as 0. Almost always a baud-rate mismatch.

Four IRQs, one interrupt vector (`UART0_IRQ = 20` in the NVIC). The handler reads `UARTMIS` (offset `0x040`) to disambiguate. Week 7.

---

## 6. Handshaking — RTS / CTS, XON / XOFF, and why neither saves you

A 115200-baud UART can move 11.5 KB/s. A USB-CDC virtual serial port can move ~1 MB/s. A typical SD-card write can stall for 50 ms in the middle of a burst. The bandwidth mismatch between "byte arrives on UART RX" and "byte gets written to flash / network / display" is the root cause of every "my UART drops bytes" bug in firmware.

There are three textbook answers:

### Hardware flow control (RTS/CTS)

The transmitter does not transmit unless the receiver has asserted its **RTS** (Request To Send) line, which the transmitter sees as **CTS** (Clear To Send). When the receiver's RX FIFO crosses a high-water mark, it deasserts RTS; the transmitter sees CTS go high, finishes the byte in flight, and stops.

On the RP2040, you wire two extra pins: `UART0_RTS` on GP3 (function F2) and `UART0_CTS` on GP2 (function F2). You set `CTSEN` and `RTSEN` in `UARTCR` (bits 14 and 15). The PL011 then handles the flow control entirely in hardware.

This is the right answer for **device-to-device** UART when both ends speak hardware flow control. It is wrong for **device-to-USB-serial-bridge**, because most USB-serial bridges (CP2102, CH340, FT232) implement RTS/CTS only when explicitly configured on the host, and most terminal programs default off.

### Software flow control (XON/XOFF)

The receiver sends a single byte `0x13` (XOFF) on its own TX line to ask the transmitter to stop, and `0x11` (XON) to resume. The transmitter parses incoming bytes for these values and pauses its own send.

This is older, slower (you must round-trip a byte to pause), and breaks any protocol that legitimately wants to send the bytes `0x11` or `0x13` (which is most binary protocols). Avoid in 2026 unless you are talking to a 1990s-vintage device that demands it.

### Don't block

The most common modern answer: **make the firmware non-blocking, so the UART RX is serviced before the FIFO can overflow.** Use an IRQ on RXIM, drain the FIFO into a ring buffer, let the application thread read from the ring buffer. The PL011's 32-byte FIFO gives you ~22 ms at 115200 baud to service the IRQ before bytes are lost. Plenty.

This is what we will build in Week 7. This week, polled is fine because we are only transmitting (Exercise 2 echoes one byte at a time, well below the FIFO limit).

The C7 voice: **for a debug log, you do not need flow control. For a protocol channel, you need IRQs and a ring buffer. For a USB-serial bridge to a terminal, you need neither because the terminal can always keep up with 11.5 KB/s.** Pick the layer that matches the use case and do not pay for what you do not need.

---

## 7. Common Week-2 UART bugs and how to spot them on a Saleae

Five canonical failure modes. The reason we capture every UART on a logic analyzer is so we can spot these in 10 seconds instead of 30 minutes.

### Bug 1 — Wrong baud rate

**Symptom:** Terminal shows garbage; one character in every ~8 is correct by chance. The Saleae's Async Serial decoder shows "framing error" on every character.

**Diagnosis:** Measure the actual bit period on the trace. If it is, say, **17.36 µs** instead of 8.68 µs, you are at **57600 baud, not 115200**. Either your terminal or your firmware has the wrong divisor.

### Bug 2 — Inverted line

**Symptom:** Terminal shows continuous garbage, or a stream of `~` (`0x7e`) characters when nothing is being sent. The Saleae decoder shows the line idle-low instead of idle-high.

**Diagnosis:** Some USB-serial bridges (notably the older FT232RL with "invert TX" jumpered, and many Bluetooth-serial modules) ship inverted. Check the trace's idle level. Fix by un-jumpering or by enabling `UARTLCR_H` bit ??? (the PL011 does *not* have a TX-invert bit — you must fix it on the bridge end).

### Bug 3 — Ground reference floating

**Symptom:** Most characters decode, ~5% are random. The trace shows the high and low levels drifting up and down by ~200 mV over time.

**Diagnosis:** You forgot to tie the bridge's GND to the Pico's GND. The line is being level-detected against a slightly different reference. This kills UART reliability faster than anything else. **Always wire GND first.**

### Bug 4 — Stop-bit truncation

**Symptom:** Terminal shows correct character then random garbage on the *next* character only. The Saleae decoder shows framing error every ~10 characters.

**Diagnosis:** Your firmware is writing the next byte to `UART0_DR` before the previous stop bit has fully shifted out. Cause: you ignored `TXFF` and assumed `TXFE` was a good "ready to send" signal. (`TXFE` means *empty*, including the shift register; `TXFF` means *full*, including the FIFO. To write safely, wait while `TXFF == 1`.)

### Bug 5 — Bytes lost during long log line

**Symptom:** A 200-byte log line comes through perfectly for the first ~32 bytes, then drops the rest. The Saleae trace shows the line stops mid-character.

**Diagnosis:** TX FIFO depth is 32 bytes (datasheet §4.2.1). Your `puts()` filled the FIFO, returned, and your application code moved on; the FIFO drained but no further bytes came. Your loop probably wrote a fixed-length buffer and stopped checking `TXFF`. Fix: keep waiting while `TXFF` is set, write the next byte, repeat.

The first time you hit each of these on the bench, write the diagnosis in your notes. The second time you hit one, you will recognize it from the trace in ~5 seconds. That is the whole point of building the muscle this week.

---

## 8. What to do this week with what you learned

Three concrete drills:

1. **Exercise 2** asks you to bring up UART0 at 115200 8N1, echo each received byte back, and measure the bit period on a Saleae trace. The acceptance is `8.681 µs ± 1%`.
2. **The mini-project's UART path** must use either the SDK's `uart_init()` (acceptable) or your own register-level init (preferred for at least the divisor write — that's the load-bearing register write in the register table).
3. **The register table artifact** must include `UARTIBRD`, `UARTFBRD`, `UARTLCR_H`, and `UARTCR` with addresses, your computed divisor values, and a one-line note next to each saying what it does.

---

## 9. Up next

Lecture 3 — The Pi Pico SDK Abstraction — reads the SDK source for `gpio_init`, `gpio_put`, `uart_init`, and `uart_puts`, and shows you that they compile to exactly the register writes you wrote in Lectures 1 and 2. The point of the abstraction is not magic; it is **consistency, type safety, and portability across the two GPIO banks and two UART blocks.** Once you trust the SDK, you can stop writing register code in production. Until you trust it, you must.

Before moving on, write the four UART addresses on the same sticky note from Lecture 1:

```
   UART0_BASE     0x4003_4000
   UART0_DR       0x4003_4000   (data)
   UART0_FR       0x4003_4018   (flags)
   UART0_IBRD     0x4003_4024
   UART0_FBRD     0x4003_4028
   UART0_LCR_H    0x4003_402c
   UART0_CR       0x4003_4030
```

You will use these for the rest of C7.
