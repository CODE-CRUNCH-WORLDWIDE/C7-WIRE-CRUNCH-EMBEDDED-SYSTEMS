# Week 1 — Exercises

Three bench drills. Do them in order, with the board in front of you. Each names an estimated time at the top; that time assumes nothing on your machine works yet.

1. **[Exercise 1 — Toolchain install](exercise-01-toolchain-install.md)** — install and prove `arm-none-eabi-gcc`, `openocd`, `probe-rs`, `gdb`, the `pico-sdk`. (~2 h)
2. **[Exercise 2 — First blink in C](exercise-02-first-blink-c.md)** — Pi Pico W blink via the official `pico-sdk`, CMake, flashed by BOOTSEL + UF2 and again by `probe-rs`. (~1.5 h)
3. **[Exercise 3 — First blink in MicroPython](exercise-03-first-blink-micropython.md)** — same blink, in MicroPython; record size and boot-time comparison. (~1 h)

## Workflow

- Type every command. Do not copy-paste blindly. Reading the command and typing it is half the lesson.
- Note your timing. The first toolchain install will take 1.5 – 3 hours depending on platform. The second time (the next time you sit at a clean machine) should take ~20 minutes. Track the delta.
- Capture artifacts. Each exercise asks for a screenshot, a scope trace, or a `size` output committed to your Week 1 repo. The bring-up note mini-project will reference these files; do not throw them away.

## Self-grading

After each exercise, ask: "Could I redo this from a blank macOS / Ubuntu install in under 30 minutes?" If yes, move on. If no, re-do it. Toolchain bring-up is the kind of thing you become fluent at only by doing it a third or fourth time.

## A note on platform

These exercises target macOS (Apple Silicon and Intel), Ubuntu 22.04+, and Windows-with-WSL2. **Pure Windows is not supported in C7.** If you are on Windows, install WSL2 and follow the Linux path. The Microsoft-provided WSL USB-passthrough (`usbipd`) is documented in Exercise 1.
