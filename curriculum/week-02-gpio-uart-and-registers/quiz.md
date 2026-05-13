# Week 2 — Quiz

Ten questions. Datasheets closed. Cite specific units and register addresses where asked. Aim for 9 / 10.

---

**Q1.** Which register lives at address `0xd000_001c` on the RP2040?

- A) `SIO_GPIO_OUT` — read-write register holding the current output value of all 30 GPIOs.
- B) `SIO_GPIO_OUT_SET` — write 1-bits set the corresponding GPIO output bits.
- C) `SIO_GPIO_OUT_CLR` — write 1-bits clear the corresponding GPIO output bits.
- D) `SIO_GPIO_OUT_XOR` — write 1-bits toggle the corresponding GPIO output bits.

---

**Q2.** Out of reset on the RP2040, every GPIO pin defaults to which state?

- A) Output, driving 0 V.
- B) Output, driving 3.3 V.
- C) Input, high-Z, with `FUNCSEL = 0x1f` (NULL — pad disconnected from all peripherals).
- D) Output, in 8 mA drive mode, ready to toggle.

---

**Q3.** You want to enable just GP15 as an output without disturbing GP0..GP14 or GP16..GP29. Which single write does this?

- A) `*(volatile uint32_t *)0xd000_0020 = 0x0000_8000` (writing to `GPIO_OE`).
- B) `*(volatile uint32_t *)0xd000_0024 = 0x0000_8000` (writing to `GPIO_OE_SET`).
- C) `*(volatile uint32_t *)0xd000_0024 = 0xffff_ffff` (writing all-ones to `GPIO_OE_SET`).
- D) `*(volatile uint32_t *)0xd000_0028 = 0x0000_8000` (writing to `GPIO_OE_CLR`).

---

**Q4.** The RP2040 UART block on UART0 is implemented as the ARM PrimeCell PL011. The bit period at 115200 baud is approximately:

- A) 8.681 µs.
- B) 1.085 µs.
- C) 86.81 µs.
- D) 100 µs (defined by the PL011 hardware, independent of baud rate).

---

**Q5.** With `clk_peri = 125 MHz` and target baud `115200`, the PL011 baud-rate divisor formula `BAUDDIV = clk_peri / (16 × baud)` yields `67.8168…`. The correct register values to program are:

- A) `UARTIBRD = 68`, `UARTFBRD = 0`.
- B) `UARTIBRD = 67`, `UARTFBRD = 52`.
- C) `UARTIBRD = 67`, `UARTFBRD = 8` (just the fraction's first digit, rounded).
- D) `UARTIBRD = 115200`, `UARTFBRD = 16`.

---

**Q6.** On a Saleae trace of UART0 at 115200 8N1, you measure the bit period as **17.36 µs**. The most likely cause is:

- A) Your terminal is running at 921600 baud and your firmware at 115200.
- B) Your firmware is running at 57600 baud (half of 115200).
- C) Your firmware is running at 230400 baud (double of 115200).
- D) The Saleae is sampling too slowly to resolve the bit edges.

---

**Q7.** The atomic-alias offsets `+0x1000`, `+0x2000`, `+0x3000` on the RP2040 implement, in hardware, which operations?

- A) `+0x1000` = SET, `+0x2000` = CLR, `+0x3000` = XOR.
- B) `+0x1000` = XOR, `+0x2000` = SET, `+0x3000` = CLR.
- C) `+0x1000` = AND, `+0x2000` = OR, `+0x3000` = NAND.
- D) `+0x1000` = increment, `+0x2000` = decrement, `+0x3000` = reset.

---

**Q8.** You implement a tactile-button debounce with a 20 ms low-pass filter. A user double-taps the button at 45 ms intervals (two presses, 45 ms apart edge-to-edge). What does your firmware report?

- A) Two `press` events and one `release` event in between. The 20 ms filter resolves both clean presses.
- B) One `press` event only. The second tap is filtered as bounce.
- C) Three `press` events (the bounces of the second tap leak through).
- D) Zero events; the line never settles for 20 ms in either state.

---

**Q9.** The Pi Pico SDK function `gpio_put(15, 1)` is `static inline` and, when called from a tight loop, compiles to approximately how many ARM Thumb instructions on the hot path?

- A) 1 instruction (a single `STR`).
- B) ~4 instructions: `MOVS`, `LSLS` (build the mask), `LDR` (load the SIO address), `STR`.
- C) ~25 instructions (the SDK adds significant overhead for safety checks).
- D) ~50 instructions; `gpio_put` calls into a vendor HAL that is several layers deep.

---

**Q10.** You write a `while (1) { SIO_GPIO_OUT_XOR = (1u << 15); }` loop on a stock RP2040 at 125 MHz with no compiler optimization flag changes. The approximate measured toggle rate at GP15 is:

- A) ~125 MHz — one toggle per CPU cycle.
- B) ~10–25 MHz — the loop is 2 cycles (`STR` + branch) but the visible toggle is at half that frequency.
- C) ~1 MHz — limited by the SIO peripheral.
- D) ~1 kHz — `gpio_xor_mask` adds ~125000 cycles of overhead per call.

---

## Answer key (no peeking until you have answered all 10)

1. **D** — `SIO_BASE = 0xd000_0000`; offset `0x01c` is `GPIO_OUT_XOR`. Datasheet §2.3.1.7, p. 42.
2. **C** — `GPIOn_CTRL.FUNCSEL` resets to `0x1f` (NULL). Datasheet §2.19.6.1, p. 247.
3. **B** — `GPIO_OE_SET` at `0xd000_0024`. Write 1-bits set, leaves all other bits unchanged. Datasheet §2.3.1.7, p. 43.
4. **A** — 1 / 115200 = 8.681 µs. C is the character time (10 bits = 86.81 µs); B is the bit period at 921600.
5. **B** — `IBRD = floor(67.8168) = 67`; `FBRD = round(0.8168 × 64) = 52`. Datasheet §4.2.7.4-5, p. 435.
6. **B** — 17.36 µs = 1 / 57600. The firmware is running at half the expected baud rate. Common cause: clk_peri changed but the divisor was not recomputed.
7. **B** — Datasheet §2.1.2, p. 18. XOR at `+0x1000`, SET at `+0x2000`, CLR at `+0x3000`.
8. **A** — Two clean presses with a 45 ms gap; the 20 ms filter resolves each. If the gap were < 20 ms (say, 15 ms), it would filter as one press.
9. **B** — ~4 instructions, all of which the compiler can sometimes hoist. See Lecture 3, §3.
10. **B** — Two cycles per `STR + branch` = 62.5 MHz iteration rate, but each iteration flips the pin **once**, so the visible square-wave frequency is 62.5/2 = ~31 MHz at the toggle level — in practice ~10–25 MHz with loop overhead.
