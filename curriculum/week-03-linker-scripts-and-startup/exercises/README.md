# Week 3 — Exercises

Three bench drills. Do them in order. Each names an estimated time at the top; do not skip even if you have done similar work elsewhere — the goal this week is to *read every line* of a linker script and a startup file, not to copy-paste a working one.

1. **[Exercise 1 — Read the default linker script](exercise-01-read-the-default-linker-script.md)** — open `pico-sdk`'s `memmap_default.ld` and annotate every directive, in writing. You will read 280 lines of `ld` script and produce ~100 lines of margin notes. The point is fluency in the syntax, before you write your own. (~90 min)
2. **[Exercise 2 — Write a minimal startup](exercise-02-write-a-minimal-startup.md)** — author `startup.c` from a blank file: the vector table, the reset handler, the default handler. Run it under the SDK's linker script (so you don't have to write your own yet) and prove `.bss` is zeroed before `main()`. (~120 min)
3. **[Exercise 3 — Blink without the SDK](exercise-03-blink-without-the-SDK.md)** — combine your own linker script, your own startup, the borrowed `boot2`, and your own `Makefile` to flash a blink that uses zero `pico-sdk` source. The output `.uf2` is yours. (~180 min)

## Workflow

- Type every command. Copy-paste from the lecture is acceptable for the first reading; type every line on the second reading.
- After each exercise, run `arm-none-eabi-nm -n -S build/<artifact>.elf | head -20` and confirm `Reset_Handler`, `__isr_vector`, `_estack`, `_sidata`, `_sdata`, `_edata`, `_sbss`, `_ebss` all resolve to the addresses you expect.
- Open the `.map` file. Skim, then search for the symbols you care about. Map-file fluency is the difference between a 5-minute debugging session and a 5-hour one.

## Self-grading

After each exercise, ask: "Could I, from a blank file, write the artifact I just produced?" If yes, move on. If no, re-do the exercise from the empty file, with the lecture closed.

## A note on safety

You will be writing your own startup and linker script for a real Cortex-M0+ chip. A wrong vector table entry produces a HardFault loop at boot — recoverable by BOOTSEL+drag-and-drop, but invisible without UART output. Wire up UART0 (GP0 = TX) to your USB-serial bridge before Exercise 3 and have a terminal open at 115200 8N1 so you can see the boot banner. If you do not see the banner, you have a startup-file bug, not a peripheral-driver bug — *look at the vector table first*.

The on-board LED on Pi Pico W is behind the CYW43439 and not reachable without the SDK's CYW43 driver. For this week we use **GP15** as the blink pin, exactly as in Week 2.
