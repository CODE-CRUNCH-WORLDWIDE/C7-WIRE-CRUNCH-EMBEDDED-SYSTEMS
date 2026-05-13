# Week 3 — Homework

Six problems, ~6 hours total. Commit each in your Week 3 repo under `notes/`. The mini-project references these files; do not throw them away.

---

## Problem 1 — Draw the RP2040 memory map from memory (45 min)

Without opening the datasheet, produce `notes/memory-map-from-memory.md` containing an ASCII drawing of the RP2040 memory map with at least 8 labeled regions: ROM, XIP flash, SRAM (striped), SRAM4/SRAM5 scratchpads, APB peripherals, AHB peripherals, SIO, PPB. Each region should include base address and length.

Then *open* the datasheet (§2.2, p. 23, Sep-2024 rev) and produce a side-by-side diff: what did you get right, what did you get wrong, what did you forget. Commit the diff.

**Acceptance.** A two-column file: column 1 is your from-memory map; column 2 is the datasheet's map. At least one substantive error or omission is acknowledged. The point of this exercise is to find your gaps.

---

## Problem 2 — Walk a real linker script line by line (1 h)

Take your Exercise 1 annotated `memmap_default.ld` and expand the annotation. For each output section (`.boot2`, `.flash_begin`, `.text`, `.rodata`, `.binary_info`, `.data`, `.uninitialized_data`, `.bss`, `.heap`, `.stack`, `.scratch_x`, `.scratch_y`), produce a one-paragraph commentary answering:

1. What input sections does it pull in?
2. Which memory region holds the VMA?
3. Which memory region holds the LMA (if different)?
4. What symbols does it export, and which file (startup, application, libc) consumes them?
5. What would break if you removed this section from the script?

Save as `notes/linker-script-deep-walk.md`. Minimum 12 paragraphs (one per output section).

---

## Problem 3 — Write a `Makefile` from scratch (1 h)

Without copying from Exercise 3, write your own `Makefile` that builds your bare-metal blink. Constraints:

- ≤ 60 lines (including comments).
- Uses GNU make pattern rules (`$(BUILD)/%.o: %.c`).
- Produces `build/blink.uf2`, `build/blink.bin`, `build/blink.map`.
- Has a `clean` target.
- Has a `flash` target that calls `picotool load -f`.
- Does not use any external Make include files (no `pico_sdk_init.cmake`-like trickery).

Save as `notes/Makefile.handwritten`. In the same directory, write `notes/makefile-walkthrough.md` annotating every line: what does this rule produce, what are its dependencies, what command does it issue.

**Acceptance.** The Makefile builds. The annotated walkthrough is at least 60 lines of explanation. (Yes, the walkthrough is longer than the Makefile.)

---

## Problem 4 — Disassemble the vector table (45 min)

Build your Exercise 3 firmware. Run:

```bash
arm-none-eabi-objdump -d -j .isr_vector build/blink.elf > vectors.asm
```

The `.isr_vector` section disassembles as a sequence of 32-bit words, each labeled with the symbol name they point to. Annotate every word:

- Word 0: `_estack` — the initial SP. What address?
- Word 1: `Reset_Handler` — the address of your reset handler.
- Words 2-15: the architectural exception handlers.
- Words 16-41: the RP2040 IRQs.

Save as `notes/vector-table-walkthrough.md`. Cite the RP2040 datasheet §2.3.2, p. 56 (Sep-2024 rev) for the IRQ assignments and DUI 0662B §2.3.4 for the architectural slots.

**Acceptance.** A file with every word annotated. At least 30 entries (16 architectural + 14 RP2040 IRQs). The `_estack` address is documented as `0x2004_0000` (or whatever your script chose).

---

## Problem 5 — Modify the linker script and watch it break (45 min)

Take your `pico.ld` from Exercise 3. Make three deliberate breaks, one at a time, and document the symptom:

1. **Remove `KEEP()` from the `.isr_vector` block.** Rebuild. Run `objdump -h` and confirm `.isr_vector` size is 0 (or much smaller). Flash and try to boot — it should not boot.
2. **Change `> RAM AT > FLASH` to `> FLASH` on the `.data` section** (placing both VMA and LMA in flash). Rebuild. Run the firmware — initialized globals should still work on first boot (because flash holds the value), but writes to them should silently fail.
3. **Change the `_estack` definition to `ORIGIN(RAM) + 0x100` (only 256 bytes of stack).** Rebuild. Run the firmware — depending on stack depth, it may HardFault on the first function call.

For each break, capture: the linker-script diff, the new `objdump -h` output, and the symptom (does it boot? does the LED toggle? does the UART work?). Save as `notes/intentional-breaks.md`.

**Acceptance.** Three documented breaks, each with diff, build output, and symptom. This is the cheapest possible debugging practice.

---

## Problem 6 — Reflection (30 min)

Write `notes/week-03-reflection.md` (300–400 words) answering:

1. What is the single linker-script directive you can now write from memory? (If the answer is "none," go back to Lecture 2 and re-do Exercise 1.)
2. What was the biggest surprise about the C runtime — the `.data` copy, the `.bss` zero, the vector table layout, or the realization that `main()` is not the first thing to run?
3. What is your current intuition for when to write your own linker script vs use the vendor's? Give one example of each.
4. What is one thing about the boot chain (boot ROM → boot2 → reset handler → main) that you now understand differently than before this week?
5. One thing you want to go deeper on after this week.

This reflection is part of the Week 3 mini-project artifact. Reviewers read it.
