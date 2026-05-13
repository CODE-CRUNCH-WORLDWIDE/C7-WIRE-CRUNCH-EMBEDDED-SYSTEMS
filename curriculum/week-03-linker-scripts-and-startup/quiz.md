# Week 3 — Quiz

Ten questions. Datasheets and linker-script docs closed. Cite specific addresses and section names. Aim for 9 / 10.

---

**Q1.** What is the *first* address the Cortex-M0+ processor reads on power-up reset, and what is loaded from that address?

- A) `0x0000_0000` — the first instruction the CPU fetches.
- B) `0x0000_0000` — the initial value of `SP` (the Main Stack Pointer).
- C) `0x1000_0000` — the entry point of the second-stage bootloader.
- D) `0x0000_0004` — the address of the reset handler.

---

**Q2.** On the RP2040, where does the boot ROM expect to find the second-stage bootloader (`boot2`)?

- A) `0x0000_0000`, the first 256 bytes of the mask-programmed boot ROM.
- B) `0x1000_0000`, the first 256 bytes of XIP flash, with a 4-byte CRC in the last 4 bytes.
- C) `0x2000_0000`, copied to SRAM by an external loader.
- D) `0xe000_0000`, in the System region (PPB).

---

**Q3.** In a linker script, the statement `.data : { *(.data*) } > RAM AT > FLASH` does what?

- A) Places `.data`'s bytes in flash, and its runtime references resolve to flash addresses.
- B) Places `.data`'s bytes in RAM, and its runtime references resolve to RAM addresses.
- C) Places `.data`'s VMA in RAM (runtime references resolve to RAM) and its LMA in FLASH (initial values stored in flash for the startup file to copy at boot).
- D) Mirrors `.data` to both flash and RAM at runtime, with hardware-coherent copies.

---

**Q4.** The `KEEP(*(.isr_vector))` directive in a linker script is needed because:

- A) Without it, the vector table would be placed at the wrong address.
- B) Without it, `--gc-sections` would remove the vector table (no C code references it; it is reached only by the hardware via an absolute address).
- C) Without it, the linker would emit the vector table twice, once for each Cortex-M0+ core.
- D) Without it, the C compiler would optimize away the vector table as "unused."

---

**Q5.** On a Cortex-M0+, what is at word 0 of the vector table (offset 0x000 in `__isr_vector`)?

- A) A function pointer to `NMI_Handler`.
- B) A function pointer to `Reset_Handler`.
- C) The 32-bit value loaded into MSP (the Main Stack Pointer) at hardware reset.
- D) A magic number that identifies the firmware as Cortex-M0+ compatible.

---

**Q6.** The `Reset_Handler` in a typical bare-metal Cortex-M firmware performs which sequence of steps before calling `main()`?

- A) Reset the CPU; configure the clocks; initialize the NVIC; call `main()`.
- B) Copy `.data` from flash LMA to SRAM VMA; zero `.bss`; (optionally) call C++ static constructors via `__libc_init_array`; call `main()`.
- C) Run `boot2`; configure XIP; jump to `main()`.
- D) Set up the heap; initialize libc; call `main()`; return cleanly.

---

**Q7.** If you forget to zero `.bss` in your `Reset_Handler`, what symptom is most likely?

- A) The firmware will not boot at all; the boot ROM will reject it.
- B) Initialized globals (`int x = 5;`) will read as zero instead of 5.
- C) Uninitialized globals (`int y;`) will read as undefined garbage instead of zero, leading to intermittent bugs.
- D) The stack will overflow on the first function call.

---

**Q8.** Which of these symbols, conventionally exported by a Cortex-M linker script, holds the initial value of the stack pointer?

- A) `Reset_Handler`
- B) `_estack` (or `__StackTop` in newlib's naming)
- C) `__bss_start__`
- D) `__data_end__`

---

**Q9.** On the RP2040, the `VTOR` register (Vector Table Offset Register) lives at which address, and what does it do?

- A) `0x4000_0000`; it holds the current XIP base address.
- B) `0xe000_ed08`; it relocates the vector table to a non-default address (e.g. for a bootloader-to-app handoff).
- C) `0xd000_0000`; it enables the single-cycle I/O bus.
- D) `0x1000_0100`; it is the start of the default vector table.

---

**Q10.** A `.map` file produced by `arm-none-eabi-ld -Map=blink.map` shows:

```
.data           0x0000000020000000        0xc  load address 0x000000001000019c
```

Which statement is correct?

- A) `.data` is 0xc = 12 bytes long; the bytes live in flash at `0x1000_019c` and are read from there at runtime.
- B) `.data` is 0xc bytes long; the bytes live in flash at `0x1000_019c` (LMA) and the program reads them at SRAM address `0x2000_0000` (VMA), after the `Reset_Handler` copies them.
- C) `.data` is 12 bytes long but the linker emitted it at two different addresses by mistake — the build is broken.
- D) `.data` occupies both `0x2000_0000` and `0x1000_019c` simultaneously, with the hardware mirroring writes.

---

## Answer key (no peeking until you have answered all 10)

1. **B** — The Cortex-M0+ loads `MSP` from `[0x0000_0000]` and `PC` from `[0x0000_0004]` on reset. So the *first* read is the SP at word 0 of the vector table, which on the RP2040 lives in the boot ROM (it just points the next stage's stack into SRAM). DUI 0662B §2.3.4, p. 2-17.
2. **B** — RP2040 datasheet §2.8.1, p. 132. The boot ROM reads `[0x1000_0000, 0x1000_0100)` into SRAM, verifies the trailing 4-byte CRC over the first 252 bytes, and jumps into the copied stub.
3. **C** — `> RAM AT > FLASH` is the canonical VMA-in-RAM, LMA-in-FLASH split for `.data`. The runtime accesses RAM; the boot-time bytes live in flash. GNU ld §3.6.8.2 (Output Section LMA).
4. **B** — `--gc-sections` removes ELF input sections that no other section references. The vector table is referenced only by hardware (the boot ROM jumps via the absolute address), not by any C code, so it appears unreferenced. `KEEP()` exempts it.
5. **C** — DUI 0662B §2.3.4, p. 2-17. Word 0 is the initial SP, not a function pointer. The processor loads it into MSP at reset.
6. **B** — The standard C-runtime startup sequence. See Lecture 3, §4.
7. **C** — Initialized globals come from `.data` (which has its own copy mechanism). Uninitialized globals (and `static` locals without initializers) live in `.bss`, which the C standard mandates be zero-initialized. Skipping the zero loop violates the standard and produces non-deterministic behavior.
8. **B** — `_estack` is the convention used by most ARM-embedded scripts; `__StackTop` is the newlib/CMSIS variant. Both point to the top of usable SRAM (the stack grows down from there).
9. **B** — Cortex-M0+ Generic User Guide §4.4.4 (VTOR), p. 4-19. `VTOR` lives at `0xe000_ed08`. Its default value is 0; writing a new address relocates the vector table.
10. **B** — Two addresses on one line: VMA (`0x2000_0000`) and LMA (`0x1000_019c`). This is exactly the VMA/LMA split that `> RAM AT > FLASH` produces. The Reset_Handler copies 12 bytes from `0x1000_019c` to `0x2000_0000` at boot.
