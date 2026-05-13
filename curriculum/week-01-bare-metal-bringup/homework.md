# Week 1 — Homework

Six problems, ~6 hours total. Commit each in your Week 1 repo under `notes/`. The mini-project references these files; do not throw them away.

---

## Problem 1 — Map the RP2040 datasheet (45 min)

Produce `notes/rp2040-map.md`, a one-page Markdown summary of the RP2040 datasheet (September 2024 revision, 640 pages).

**Acceptance.** A table with at least 10 rows, each row listing a major section (§1, §2.1, §2.2, …), the page range, a one-sentence "what's in it," and one specific register, page number, or block diagram you found notable.

Example row:
| § | Pages | What's in it | Notable |
|---|---|---|---|
| §2.19 | 240–265 | GPIO and IO_BANK0 — pin mux, pad control, drive strength | Function-select table on p. 244 |

---

## Problem 2 — Pi Pico vs Pi Pico W diff (45 min)

Read the **Pi Pico** datasheet (~30 pages, the original, not the W) and the **Pi Pico W** datasheet (also ~30 pages). Diff them.

**Acceptance.** A file `notes/pico-vs-picow.md` with:
- A side-by-side table of differences (at least: on-board LED wiring, wireless, current draw, antenna, pinout if any).
- A one-paragraph answer to: "If a friend hands you a Pico and a Pico W and says 'one of these blinks, one does not,' what is the *first* line of code you would suspect?"
- A note on whether the Pico H or Pico WH (the pre-soldered-header variants) differ electrically from their plain counterparts. (Hint: they do not, but verify.)

---

## Problem 3 — `arm-none-eabi-size` and the SDK overhead (45 min)

Take your Exercise-2 blink. Produce three builds, each with one variable changed, and record the `size` output of each:

1. The baseline `pico_stdlib + pico_cyw43_arch_none` build.
2. The same code with `pico_cyw43_arch_none` removed and the LED line stripped (so it builds but does nothing useful). What does the baseline SDK cost?
3. The same code compiled with `-Os` instead of the default `-O2`. (Edit `CMakeLists.txt`: `target_compile_options(blink PRIVATE -Os)`.)

**Acceptance.** A file `notes/size-comparison.md` with a 3-row table (config, text, data, bss, total), plus a one-paragraph reflection on which optimization had the biggest single impact, and why you would or would not ship `-Os` in production.

---

## Problem 4 — Find one register in two parts (1 h)

Pick a peripheral that exists on both the RP2040 *and* an STM32F4 part you can browse online (the STM32F446 reference manual RM0390 is free on st.com, ~1,800 pages — search instead of skim). UART is the easiest.

**Acceptance.** A file `notes/uart-register-diff.md` with:
- The RP2040 UART0 data register: full name, address, bit fields, datasheet citation.
- The STM32F446 USART2 data register: full name, address, bit fields, RM citation.
- A short table (≤ 8 rows) comparing the two: clock source, FIFO depth, max baud, DMA support, parity options.
- A one-paragraph answer to: "Why does the STM32 have ~5× more configuration bits than the RP2040 here?"

You will not be able to answer the last question with certainty. A speculation paragraph is fine — name the trade-offs.

---

## Problem 5 — Push the C blink to 1 kHz, then to as fast as you can (1 h)

Take your Exercise-2 blink. Change `sleep_ms(500)` to `sleep_ms(1)`. Build. Flash. Is the LED still visible? (It should not be — at 500 Hz toggle rate it appears continuously on.) Now use `sleep_us(500)` for a 1 kHz toggle. Probe GP15 (which you toggle alongside the LED) on a scope or Saleae and confirm a clean 1 kHz square wave.

Then push faster:
- `sleep_us(50)` → ~10 kHz toggle. Does it still look square?
- Replace `sleep_us(50)` with a single `__asm__("nop");` in a tight loop. What rate do you get?
- Replace the `gpio_put` calls with direct SIO writes: `sio_hw->gpio_togl = 1u << 15;` (the SIO base is `sio_hw` from the SDK; the `gpio_togl` register XORs the GPIO output). Measure again.

**Acceptance.** A file `notes/blink-fast.md` with a 4-row table (method, measured toggle rate in Hz, scope screenshot link, notes). One sentence on the surprise.

---

## Problem 6 — Reflection (30 min)

Write `notes/week-01-reflection.md` (300–400 words) answering:

1. What was your time-to-first-blink, end-to-end (from `git clone pico-sdk` to a working LED)?
2. Which step took longest, and was it tooling, hardware, or comprehension?
3. What is one thing about an MCU you expected to know that turned out to be wrong?
4. What is one thing about the Pi Pico W specifically that surprised you?
5. One thing you want to go deeper on after this week.

This reflection is part of the Week-1 bring-up note. Reviewers read it.

---

## Time budget

| Problem | Time |
|--------:|-----:|
| 1 | 45 min |
| 2 | 45 min |
| 3 | 45 min |
| 4 | 1 h |
| 5 | 1 h |
| 6 | 30 min |
| **Total** | **~5 h** |

When done, push your Week 1 repo and start the [mini-project](./mini-project/README.md).
