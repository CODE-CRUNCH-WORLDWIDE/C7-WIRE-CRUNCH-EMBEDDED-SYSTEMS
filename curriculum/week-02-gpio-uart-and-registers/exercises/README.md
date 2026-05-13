# Week 2 — Exercises

Three bench drills. Do them in order, with the board in front of you and a logic analyzer connected. Each names an estimated time at the top; assume nothing about your prior bench setup.

1. **[Exercise 1 — Toggle GPIO by register](exercise-01-toggle-gpio-by-register.md)** — write to `SIO_GPIO_OUT_XOR` at `0xd000_001c` directly, with no `pico-sdk` GPIO calls; measure the resulting toggle rate on a logic analyzer. (~2 h)
2. **[Exercise 2 — UART echo](exercise-02-uart-echo.md)** — bring UART0 up at 115200 8N1, echo each received byte back, and verify the bit period on a Saleae trace against the 8.681 µs target. (~2 h)
3. **[Exercise 3 — Button debounce](exercise-03-button-debounce.md)** — wire a tactile push-button to GP15, implement a 20 ms software debounce, and produce an edge-triggered state machine that survives a 1000-press soak. (~2 h)

## Workflow

- Type every command. Reading a magic-number address and typing it is half the lesson; this week especially.
- Capture every signal on a logic analyzer. By Sunday your `traces/` directory has at least six `.sal` or `.sr` files in it.
- After each exercise, add the registers you touched to a running `notes/registers-touched.md` file. The mini-project's `REGISTER-TABLE.md` is this file, polished.

## Self-grading

After each exercise, ask: "Could I write down, from memory, the addresses and reset values of the registers I just touched?" If yes, move on. If no, re-read the relevant lecture and re-do the bench step. The point of this week is fluency in register-land, not a working firmware.

## A note on safety

You will be writing to addresses below `0x4000_0000` and around `0xd000_0000`. The Cortex-M0+ has no MMU and no memory protection on the RP2040. A write to the wrong address will not crash — it will silently change a peripheral's state, or be ignored, or in rare cases lock a peripheral until reset. If your firmware misbehaves and you can't see why, BOOTSEL-reset the board and re-flash a known-good image. There is no "lose the board" risk from a bad register write.
