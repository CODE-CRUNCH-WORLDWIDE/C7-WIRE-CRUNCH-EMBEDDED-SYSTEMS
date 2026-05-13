# Week 2 — GPIO, UART, and Peripheral Registers

> *Your blink works because someone, somewhere, wrote the right 32 bits to the right 32-bit address. This week you become that someone.*

Welcome to Week 2 of C7. Last week you brought up a Pi Pico W and proved the toolchain, the board, and the bring-up note all hold together. This week we take the lid off. You will stop calling `gpio_put()` and start writing to `SIO_GPIO_OUT_XOR` at `0xd000_001c` directly. You will stop trusting that `uart_puts()` "just works" and instead walk a single byte from the `UART0_DR` register at `0x4003_4000` through the baud-rate divisor and out onto a wire that a logic analyzer can decode. By Sunday you will be able to flip a GPIO three different ways (SDK, register write, atomic XOR) and tell the cohort reviewer which one belongs in a shipped product and why.

This is the most important week in Phase I. Every later peripheral — SPI, I2C, CAN, USB, ADC, DMA — is the same shape as GPIO and UART: a base address, a register table, a clock, an enable bit, an IRQ line. Internalize the shape this week. The rest of the course is variations on it.

---

## Learning objectives

By the end of this week, you will be able to:

- **Identify** the four register banks that govern an RP2040 GPIO pin (`IO_BANK0`, `PADS_BANK0`, `SIO`, the per-pin `GPIOn_CTRL`) and explain which bit of which register is responsible for output drive, pull, function-select, slew rate, and IE/OE behavior.
- **Locate** `SIO_GPIO_OUT`, `SIO_GPIO_OUT_SET`, `SIO_GPIO_OUT_CLR`, and `SIO_GPIO_OUT_XOR` at offsets `0x10`, `0x14`, `0x18`, and `0x1c` from `SIO_BASE = 0xd000_0000` in the RP2040 datasheet (Sep-2024 rev, §2.3.1.7 starting on p. 41) without using the search bar.
- **Write** a freestanding C function that toggles GP15 using only a `volatile uint32_t *` cast — no `pico-sdk` calls — and verify the bit pattern on a logic analyzer.
- **Compute** the UART baud-rate divisor `UARTIBRD:UARTFBRD` for any (peripheral clock, baud) pair, given the 6-bit fractional divisor formula in the ARM PrimeCell PL011 specification that the RP2040 inherits (RP2040 RM §4.2.7.1, p. 432 of the Sep-2024 rev), and confirm the actual bit period on a Saleae trace within 1% of the target.
- **Explain** the bit-level shape of one UART character on the wire: start bit (low for one bit time), eight data bits LSB-first, no parity, one stop bit (high), and the consequences of each receiver clock-recovery oversampling choice (PL011 uses 16x by default).
- **Implement** a software debounce for a tactile push-button on GP15, with a 20 ms stable-low timer, an edge-detected state machine, and three documented failure modes (contact bounce, EMI, mechanical bounce-on-release).
- **Bit-bang** an SPI clock and MOSI line at 1 MHz using only direct GPIO writes, and drive an SSD1306 OLED to display the text `crunch-wire w02 boot`. The hardware SPI block exists; you will refuse to use it this week, to prove you understand what it would do for you.
- **Write** the Week 2 register table: a one-page artifact listing every register address, offset, reset value, and access type you touched, in the C7 voice. The Week 2 mini-project requires this artifact.

---

## Prerequisites

You have shipped the Week 1 bring-up note. Your `BRING-UP-NOTE.md` is on GitHub, signed off by a reviewer, and you can flash a Pi Pico W blink in under 5 minutes from a clean clone. If not, finish Week 1 first; this week assumes that muscle is built.

You have the RP2040 datasheet (Sep-2024 rev, 640 pages) downloaded locally and at least one tab open to §2.3 (SIO) and one to §4.2 (UART). Page numbers in this week's notes assume that revision.

You have a Saleae or `sigrok`/PulseView 8-channel logic analyzer on your bench. The protocol-level work this week is not learnable from a serial terminal alone — you must see the bits.

---

## Topics covered

- The four register banks of an RP2040 GPIO pin: `IO_BANK0` (function select, drive enable, input override), `PADS_BANK0` (pull, drive strength, slew, schmitt), `SIO` (the single-cycle I/O that the CPU uses for fast read/write/set/clear/xor), and the per-pin `GPIOn_CTRL` register that selects between F1 and F8.
- The single-cycle I/O block (`SIO_BASE = 0xd000_0000`): why writing to `GPIO_OUT_XOR` is two cycles, why writing to `GPIO_OUT` is also two cycles, and why neither one is on the AHB-Lite peripheral fabric like UART or SPI.
- Atomic register aliases on RP2040: every peripheral register has a `_SET` (offset `+0x2000`), `_CLR` (`+0x3000`), and `_XOR` (`+0x1000`) alias that performs a read-modify-write in hardware without needing a CPU lock. The pattern is uniform across the chip and saves you 6 cycles per RMW.
- Reading a register table: offset, reset value, R/W/RW/WC access semantics, reserved bits — and what the silicon does if you write to a reserved bit (usually: silently ignored, sometimes: locks the peripheral until reset).
- The shape of a UART character on the wire: start bit, 8 data bits LSB-first, no parity, one stop bit. The bit period at 115200 baud is **8.681 µs**. At 921600 baud it is **1.085 µs**, which is why your UART starts dropping bytes if you do not enable the FIFO and the IRQ.
- The ARM PL011 PrimeCell UART (which the RP2040 inherits): the 16x oversampling receiver, the 16-bit integer + 6-bit fractional baud-rate divisor, the 32-byte TX/RX FIFOs, and the four interrupt sources (RXIM, TXIM, RTIM, FEIM).
- Handshake basics: hardware flow control with RTS/CTS, software flow control with XON/XOFF, and why neither one helps if your firmware blocks for 200 ms inside a logging call.
- The Pi Pico SDK abstraction: what `gpio_init()`, `gpio_set_dir()`, `gpio_put()`, `uart_init()`, and `uart_puts()` actually compile to. We will read the SDK source. We will read the disassembly. We will measure the cycle count.
- Button debouncing: why a tactile push-button bounces for 5–20 ms on press and 1–5 ms on release, why a 20 ms low-pass filter is the textbook answer, and why an edge-triggered state machine beats a level-triggered poll in a real product.
- The week's decision rule: **registers for fluency, SDK for delivery, hand-rolled inline asm for nothing this week.** You will write the same blink three ways and pick one to ship.

---

## Weekly schedule

| Day       | Focus                                              | Lectures | Exercises | Challenges | Quiz/Read | Homework | Mini-Project | Bench/Self-Study | Daily Total |
|-----------|----------------------------------------------------|---------:|----------:|-----------:|----------:|---------:|-------------:|-----------------:|------------:|
| Monday    | GPIO from the register up; the SIO; pad control    |   2h     |   2h      |    0h      |   0.5h    |   1h     |     0h       |       0.5h       |     6h      |
| Tuesday   | UART bit timing; PL011; baud-rate divisor          |   2h     |   2h      |    0h      |   0.5h    |   1h     |     0h       |       0.5h       |     6h      |
| Wednesday | The Pi Pico SDK abstraction; reading the source    |   2h     |   1h      |    0.5h    |   0.5h    |   1h     |     1h       |       0.5h       |     6.5h    |
| Thursday  | Button debounce; edge state machines               |   1h     |   2h      |    0.5h    |   0.5h    |   1h     |     1h       |       0.5h       |     6.5h    |
| Friday    | SPI bit-bang challenge; OLED drive                 |   0h     |   0h      |    3h      |   0.5h    |   1h     |     1h       |       0.5h       |     6h      |
| Saturday  | Mini-project deep work; the register table         |   0h     |   0h      |    0h      |   0h      |   1h     |     3h       |       0.5h       |     4.5h    |
| Sunday    | Quiz; review; polish the register table            |   0h     |   0h      |    0h      |   0.5h    |   0h     |     0h       |       0h         |     0.5h    |
| **Total** |                                                    | **7h**   | **7h**    |  **4h**    |  **3h**   |  **6h**  |   **6h**     |     **3h**       |   **36h**   |

Self-paced cohorts compress to ~12 h/week. The load-bearing items are Lecture 1 (GPIO from the register up), Exercise 1 (toggle GPIO by register), and the mini-project (Pi Pico W producing UART log + button-controlled blink). Skip Challenge 1 (SPI bit-bang OLED) if you must; it returns as homework in Week 8.

---

## How to navigate this week

| File                                                                                         | What's inside                                                            |
|----------------------------------------------------------------------------------------------|--------------------------------------------------------------------------|
| [README.md](./README.md)                                                                     | This overview                                                            |
| [resources.md](./resources.md)                                                               | RP2040 datasheet (specific pages), Cortex-M0+ refman, Pi Pico SDK API     |
| [lecture-notes/01-gpio-from-the-register-up.md](./lecture-notes/01-gpio-from-the-register-up.md) | The four register banks; SIO; atomic SET/CLR/XOR aliases; first register write |
| [lecture-notes/02-uart-bit-timing-and-handshake.md](./lecture-notes/02-uart-bit-timing-and-handshake.md) | PL011 PrimeCell; baud-rate divisor math; the byte on the wire; flow control |
| [lecture-notes/03-the-pi-pico-sdk-abstraction.md](./lecture-notes/03-the-pi-pico-sdk-abstraction.md) | What `gpio_init`/`uart_init` compile to; reading the SDK source; cost in cycles |
| [exercises/README.md](./exercises/README.md)                                                 | Index of exercises                                                       |
| [exercises/exercise-01-toggle-gpio-by-register.md](./exercises/exercise-01-toggle-gpio-by-register.md) | Toggle GP15 with raw `volatile uint32_t *` writes; verify on logic analyzer |
| [exercises/exercise-02-uart-echo.md](./exercises/exercise-02-uart-echo.md)                   | UART0 RX → UART0 TX echo at 115200 8N1; measure bit period on Saleae      |
| [exercises/exercise-03-button-debounce.md](./exercises/exercise-03-button-debounce.md)       | Tactile push-button on GP15 with software debounce and edge state machine |
| [challenges/README.md](./challenges/README.md)                                               | Index of challenges                                                      |
| [challenges/challenge-01-spi-bitbang-an-oled.md](./challenges/challenge-01-spi-bitbang-an-oled.md) | Drive an SSD1306 OLED with a bit-banged SPI clock — no hardware SPI block |
| [quiz.md](./quiz.md)                                                                         | 10 questions, datasheet-grade, register addresses required               |
| [homework.md](./homework.md)                                                                 | Six practice problems                                                    |
| [mini-project/README.md](./mini-project/README.md)                                           | Week 2 deliverable — UART log + button-controlled blink + the register table |

---

## The Week 2 deliverable, in one line

By Sunday 23:59 local time you produce a single artifact: a public GitHub repo containing a Pi Pico W firmware that prints a structured UART log over UART0 at 115200 8N1 once per second and blinks the on-board LED at a rate controlled by a tactile push-button on GP15. The firmware must include at least one **direct register write** (no SDK wrapper) for a non-trivial operation, and the repo must include a `REGISTER-TABLE.md` listing every register touched, by name and address. A fault-model card covers at least three faults.

Week 3 of the syllabus references this artifact by name.

---

## Stretch goals

- Read the RP2040 RM §2.3 (SIO) end to end. It is 31 pages, pp. 39–70 of the Sep-2024 rev. Annotate one register field you do not yet understand and bring it to Friday studio.
- Take Exercise 1 (toggle GPIO by register) and disassemble the resulting `.elf` with `arm-none-eabi-objdump -d`. Find the instruction that performs the write to `SIO_GPIO_OUT_XOR`. Confirm it is a single `str` instruction with the address already in a register.
- Implement a polled UART RX loop that reads one byte from `UART0_DR` (`0x4003_4000`) at a time, prints it as a hex pair over the same UART TX, and measures the round-trip latency with a scope.
- Add a second tactile push-button on GP14 and make the firmware a 2-bit state machine: GP15 controls blink rate, GP14 controls log verbosity. Document the state diagram.

---

## Up next

[Week 3 — Bare-Metal Bring-Up: Linker Scripts & Startup](../week-03/) — once your register table is on GitHub and your reviewer has signed off on the UART trace.
