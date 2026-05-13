# Week 2 — Homework

Six problems, ~6 hours total. Commit each in your Week 2 repo under `notes/`. The mini-project references these files; do not throw them away.

---

## Problem 1 — Walk the SIO register table (45 min)

Open RP2040 datasheet §2.3.1.7 (Sep-2024 rev, p. 41). Produce `notes/sio-register-table.md`, a Markdown table of every SIO register related to GPIO from offset `0x000` to offset `0x07c`.

**Acceptance.** A table with at least 16 rows, each row listing:

| Offset | Register name | Reset value | Access | One-line description |
|---|---|---|---|---|

Include `GPIO_IN`, `GPIO_OUT`, `GPIO_OUT_SET/CLR/XOR`, `GPIO_OE`, `GPIO_OE_SET/CLR/XOR`. Also include the corresponding registers for the **QSPI-bank** GPIOs (offsets `0x040`–`0x07c`) — even though we will not touch them this week, knowing they exist explains the SDK's bank-selection logic.

End the file with one paragraph answering: "What is the smallest set of registers from this table that you actually need to toggle the on-board LED on Pi Pico W?" (Trick question — the LED is behind the CYW43; you cannot use SIO at all.)

---

## Problem 2 — Compute the PL011 divisor for five baud rates (45 min)

For each of the following target baud rates at `clk_peri = 125 MHz`, compute `UARTIBRD`, `UARTFBRD`, the actual baud produced, and the error percentage:

| Target baud | UARTIBRD | UARTFBRD | Actual baud | Error |
|---:|---:|---:|---:|---:|
| 9600 | ? | ? | ? | ? |
| 38400 | ? | ? | ? | ? |
| 115200 | 67 | 52 | 115181.32 | -0.016% |
| 230400 | ? | ? | ? | ? |
| 921600 | ? | ? | ? | ? |

Show the work for one row in full (which row is your choice). Save as `notes/pl011-divisor-table.md`.

End with one paragraph: "Which of these five baud rates is closest to the PL011's 2% error margin? Would you ship a firmware at that baud, or pick a different one?"

---

## Problem 3 — Disassembly drill: `gpio_put` vs raw write (45 min)

Take your Exercise 1 firmware (`toggle.c`, raw register writes). In a second file, write the same firmware using `gpio_init` and `gpio_xor_mask`. Build both with the same `CMakeLists.txt` flags. Then:

```bash
arm-none-eabi-objdump -d build/toggle-raw.elf > raw.asm
arm-none-eabi-objdump -d build/toggle-sdk.elf > sdk.asm
```

**Acceptance.** A file `notes/disassembly-compare.md` containing:
- A 20-line excerpt from each `.asm` file showing the hot path (the `while (1)` body).
- A side-by-side table: instruction count, cycle count (estimated), addresses referenced.
- One paragraph: "Are these the same machine code? Why or why not?"

You should find they are essentially identical (≤ 2 instructions difference). If they differ by more than ~5, your `CMakeLists.txt` is not in Release mode; rebuild.

---

## Problem 4 — UART baud-rate stress test (1 h)

Modify your Exercise 2 echo firmware to switch baud rates every 5 seconds, cycling through `9600`, `38400`, `115200`, `230400`, `921600`. At each rate, send `BAUD <rate> on\r\n` exactly twice, then wait, then switch.

**Acceptance.** A file `notes/baud-stress.md` with:
- A Saleae trace screenshot showing two of the five baud rates (the bit period changes visibly).
- A table: target baud, expected bit period (µs), measured bit period (µs), error %.
- One paragraph: "At which baud rate, if any, did your USB-serial bridge stop tracking the line?" (CP2102 stops at ~921600; CH340 stops at ~460800; FT232 can do 3 Mbaud).

---

## Problem 5 — Atomic XOR vs read-modify-write race (1 h)

Write a program where **core 0** toggles GP15 in a tight loop using `*(volatile uint32_t *)0xd000_0010 ^= (1u << 15)` (read-modify-write of `GPIO_OUT`), and **core 1** simultaneously toggles GP14 the same way. Run both cores via `multicore_launch_core1`. Capture both pins on a Saleae for 1 second.

Now repeat with the atomic alias: both cores write to `SIO_GPIO_OUT_XOR`, each their own bit.

**Acceptance.** A file `notes/atomic-vs-rmw.md` with:
- Two Saleae screenshots (RMW version and atomic version).
- A count of edges on each pin over the 1-second capture.
- One paragraph: did the RMW version actually drop edges? (Answer: probably yes, but you may have to run for 10 seconds to catch one — the race window is small. This is the classic "rare bug that breaks in the field" pattern.)

---

## Problem 6 — Reflection (30 min)

Write `notes/week-02-reflection.md` (300–400 words) answering:

1. What is the single register address you can now spell from memory? (If the answer is "none," go back to Lecture 1 and re-do Exercise 1.)
2. What was the biggest surprise about UART bit timing — the divisor math, the 16x oversampling, or the LSB-first byte order?
3. What is your current intuition for when to drop from the SDK down to raw registers in shipped code? Give one example.
4. What is one thing about the SSD1306 OLED (or any peripheral you bit-banged in Challenge 1) that you now understand differently than before this week?
5. One thing you want to go deeper on after this week.

This reflection is part of the Week-2 register-table artifact. Reviewers read it.
