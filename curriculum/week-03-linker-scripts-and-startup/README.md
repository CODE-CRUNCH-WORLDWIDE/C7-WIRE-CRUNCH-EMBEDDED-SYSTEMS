# Week 3 — Linker Scripts, Startup Files, and the Memory Map

> *Your `main()` does not run first. Hundreds of instructions run before `main()`. This week you write them.*

Welcome to Week 3 of C7. Last week you wrote a single 32-bit value to `SIO_GPIO_OUT_XOR` at `0xd000_001c` and watched an oscilloscope edge appear. That edge is the *last* step of a long chain. The chain starts when the RP2040 leaves the boot ROM with the program counter pointing at a 256-byte boot stage in flash, which is itself loaded by a second-stage bootloader from XIP, which jumps to the reset vector at the second word of your binary, which sets up `SP`, runs the C runtime startup, copies `.data` from flash to SRAM, zeroes `.bss`, calls `main()`. This week you write every step from the reset vector forward — your own linker script, your own startup file, your own vector table, your own `.data` copy loop — and you delete the Pi Pico SDK from the build entirely. The board still blinks. You wrote the chain.

This is the week most embedded engineers fail to internalize and then spend the next ten years not understanding why their `static const` global was at the wrong address, why their `printf` brings in 14 KB of libc they did not invite, why their first `volatile` write from a constructor segfaults before `main()` runs. By Sunday you will know. The mini-project is a from-scratch Pi Pico W blink with no SDK, no CMake, no `pico_sdk_init()` — just `arm-none-eabi-gcc`, your linker script, your startup, and a `Makefile`.

---

## Learning objectives

By the end of this week, you will be able to:

- **Draw**, from memory, the RP2040 memory map: ROM at `0x0000_0000`, XIP flash at `0x1000_0000`, SRAM at `0x2000_0000`, AHB peripherals at `0x4000_0000`, SIO at `0xd000_0000`, PPB (system control + NVIC) at `0xe000_0000`, and label which regions are executable, which are device-typed, and which are normal cacheable memory. Cite RP2040 datasheet §2.2, p. 23 (Sep-2024 rev).
- **Read** a GNU `ld` linker script line by line: `MEMORY`, `SECTIONS`, location counter `.`, output sections `.text` / `.data` / `.bss`, AT > region for VMA-vs-LMA, alignment with `ALIGN(4)`, symbol definitions like `_etext` and `__bss_start__`, and the `KEEP()` directive that keeps the vector table from being garbage-collected by `--gc-sections`.
- **Author** a linker script for the RP2040 from a blank file: a `FLASH` region at `0x1000_0000` of length 2 MiB, a `RAM` region at `0x2000_0000` of length 256 KiB, output sections placing the vector table at the start of `FLASH`, code in `.text`, initialized data with VMA in `RAM` and LMA in `FLASH`, and `.bss` zeroed in `RAM`. Cite GNU `ld` documentation §3.6 (Specifying Output Sections) and §3.4.4 (MEMORY command).
- **Write** an ARMv6-M reset handler in C: set `SP` from the first vector-table entry, copy `.data` from `_sidata` to `[_sdata, _edata)`, zero `[_sbss, _ebss)`, call any `__libc_init_array` static constructors (or skip them with justification), then call `main()`. Cite ARM Cortex-M0+ Generic User Guide (DUI 0662B) §2.3.4 (vector table) and §B1.5 (reset behavior).
- **Construct** an ARMv6-M vector table for the RP2040 of at least 48 entries: initial `SP` (word 0), reset (word 1), NMI, HardFault, reserved, ..., SysTick, then 32 external IRQs. Cite RP2040 datasheet §2.3.2, p. 56 (Sep-2024 rev) for the external IRQ assignments.
- **Explain** why the second-stage bootloader `boot2` on the RP2040 occupies the first 256 bytes of `.text` in flash and is CRC-checked by the boot ROM at `0x0000_0000` (RP2040 datasheet §2.8.1, p. 132). Show how to incorporate it into your linker script without re-implementing the QSPI XIP setup.
- **Bring up** a Pi Pico W with no SDK code in the build: `arm-none-eabi-gcc -c startup.c main.c boot2.S -mcpu=cortex-m0plus -mthumb -nostdlib` plus a linker step `arm-none-eabi-ld -T pico.ld -o blink.elf …`. Produce a `.uf2` (via `picotool` or `elf2uf2`), flash it, and watch GP15 toggle at 1 Hz with zero vendor code in your `git log`.
- **Diagnose** a bring-up failure by reading the `.map` file produced by `ld -Map=blink.map`: confirm `.text` lives in flash, `.data` is in SRAM with an LMA in flash, `.bss` is zeroed, and the symbols `_estack`, `Reset_Handler`, and `__isr_vector` resolve to the addresses you expect.

---

## Prerequisites

You have shipped the Week 2 register table. Your `REGISTER-TABLE.md` is on GitHub, your reviewer has signed off, and you can spell `0xd000_001c` from memory. If not, finish Week 2 first; this week assumes you already know what register-level GPIO looks like.

You have the RP2040 datasheet (Sep-2024 rev, 640 pages) open to §2.2 (Bus Fabric / Memory Map) and §2.8 (Boot Sequence). Page numbers in this week's notes assume that revision.

You have the ARM Cortex-M0+ Devices Generic User Guide (DUI 0662B) downloaded. It is ~110 pages and free. This week you will read §2 (The Cortex-M0+ Processor) and §B1 (Memory model + reset).

You have the GNU `ld` documentation page open. It is HTML, ~250 printed pages. We use §3.4 (MEMORY) and §3.6 (SECTIONS) directly this week.

---

## Topics covered

- The Cortex-M0+ memory map: a fixed 4 GiB address space partitioned into Code (`0x0000_0000`–`0x1FFF_FFFF`), SRAM (`0x2000_0000`–`0x3FFF_FFFF`), Peripheral (`0x4000_0000`–`0x5FFF_FFFF`), External RAM (`0x6000_0000`–`0x9FFF_FFFF`), External Device (`0xA000_0000`–`0xDFFF_FFFF`), and System (`0xE000_0000`–`0xFFFF_FFFF`). The architecture defines the regions; the silicon fills them. RP2040 puts SIO at `0xd000_0000`, breaking the convention because the SIO is on a private CPU-local bus.
- Flash vs SRAM on the RP2040: XIP (eXecute-In-Place) flash at `0x1000_0000`–`0x1100_0000` (16 MiB window, 2 MiB physical on the Pi Pico W), SRAM at `0x2000_0000`–`0x2004_2000` (264 KiB across six striped banks). Code runs from XIP; data lives in SRAM; `.rodata` may live in either.
- The four output sections every embedded firmware needs: `.text` (code + read-only data, in flash), `.data` (initialized writable data, VMA in SRAM, LMA in flash), `.bss` (zero-initialized writable data, in SRAM, no LMA), and `.stack` (top of SRAM, grows down). Optional fifth: `.heap` (for `malloc`, between `__bss_end__` and the stack).
- VMA vs LMA: the Virtual Memory Address is where a symbol *appears* at runtime (e.g. `0x2000_0000` for a global `int x = 5;`). The Load Memory Address is where the linker *places* the symbol's initial value in the binary image (e.g. `0x1000_0400` in flash, right after `.text`). The startup code's job is to copy LMA → VMA before `main()` runs. Most beginners conflate the two and produce firmware that boots once after a power cycle but not after a soft reset.
- The vector table on ARMv6-M: a contiguous array of 32-bit function pointers, starting with the initial stack pointer (word 0, not a function pointer despite being in the table), the reset handler (word 1), NMI (word 2), HardFault (word 3), 12 reserved/reserved-by-arch slots, SVCall (word 11), reserved, reserved, PendSV (word 14), SysTick (word 15), then 32 external IRQ vectors (words 16–47). On the RP2040 the external IRQs are listed in datasheet §2.3.2, p. 56. The vector table lives at the start of `.text` in flash, and the boot ROM jumps to `vector_table[1]` (the reset handler) on power-up.
- The reset handler: an ordinary C function (or assembly) that the boot ROM calls. Its responsibilities, in strict order: (1) set `SP` from `vector_table[0]` — the architecture already does this for you at reset, but if you ever take a soft reset via `AIRCR.SYSRESETREQ`, you re-enter the same path; (2) copy `.data` from LMA to VMA — a `while (src < end) *dst++ = *src++;` loop; (3) zero `.bss`; (4) invoke `__libc_init_array` if you have static C++ constructors; (5) call `main()`; (6) loop forever if `main()` returns. The whole thing is ~30 lines of C.
- The RP2040 second-stage bootloader (`boot2`): a 256-byte assembly stub that the boot ROM at `0x0000_0000` copies to SRAM, CRC-checks, and executes. Its job is to configure the QSPI flash chip's continuous-read-mode and remap XIP so that the rest of the firmware can run from flash at `0x1000_0100`. You do not write `boot2` from scratch — the RP2040 SDK ships a reference implementation in `pico-sdk/src/rp2_common/boot_stage2/`. You *do* place it at the very start of `.text` in your linker script and ensure the boot ROM's CRC includes it. Datasheet §2.8.1, p. 132.
- The `MEMORY` command in `ld`: declares the named regions of physical memory (`FLASH (rx)`, `RAM (rwx)`) with origin and length. The `SECTIONS` command places output sections into those regions with `> FLASH` or `AT > FLASH` syntax. The location counter `.` walks forward as sections are placed; you can read it (`_etext = .;`) and write it (`. = ALIGN(4);`).
- Garbage collection with `-ffunction-sections -fdata-sections -Wl,--gc-sections`: every function and global gets its own ELF section, and unreferenced sections get removed at link time. This is the standard size-saving discipline for embedded — typical 30–60% binary-size reduction. The catch: the vector table looks unreferenced (nothing in C calls it), so you wrap it in `KEEP()` in the linker script. Same for `boot2`.
- The `.map` file: the linker's audit log. Open it with any text editor; search for a symbol; read the column for VMA, LMA, size, and source object. This is the first place you look when a fault traces to "my global is at the wrong address."
- The week's decision rule: **write your own linker and startup once, per architecture, by hand. Then use the vendor's for the rest of your career.** You will never write a production linker script from scratch again after this week — but you will read other people's linker scripts on every job. The fluency starts here.

---

## Weekly schedule

| Day       | Focus                                                | Lectures | Exercises | Challenges | Quiz/Read | Homework | Mini-Project | Bench/Self-Study | Daily Total |
|-----------|------------------------------------------------------|---------:|----------:|-----------:|----------:|---------:|-------------:|-----------------:|------------:|
| Monday    | Memory map; Cortex-M0+ regions; flash vs SRAM        |   2h     |   1h      |    0h      |   0.5h    |   1h     |     0h       |       0.5h       |     5h      |
| Tuesday   | Anatomy of a linker script; `MEMORY` and `SECTIONS`  |   2h     |   2h      |    0h      |   0.5h    |   1h     |     0h       |       0.5h       |     6h      |
| Wednesday | The startup file; vector table; reset handler        |   2h     |   2h      |    0.5h    |   0.5h    |   1h     |     1h       |       0.5h       |     7.5h    |
| Thursday  | Blink without the SDK; `Makefile` from scratch       |   1h     |   2h      |    0.5h    |   0.5h    |   1h     |     1h       |       0.5h       |     6.5h    |
| Friday    | Bare-bare-metal challenge; reading the `.map` file   |   0h     |   0h      |    2h      |   0.5h    |   1h     |     1h       |       0.5h       |     5h      |
| Saturday  | Mini-project deep work; the linker-script artifact   |   0h     |   0h      |    0h      |   0h      |   1h     |     3h       |       0.5h       |     4.5h    |
| Sunday    | Quiz; review; polish the artifact                    |   0h     |   0h      |    0h      |   0.5h    |   0h     |     0h       |       0h         |     0.5h    |
| **Total** |                                                      | **7h**   | **7h**    |  **3h**    |  **3h**   |  **6h**  |   **6h**     |     **3h**       |   **35h**   |

Self-paced cohorts compress to ~12 h/week. The load-bearing items are Lecture 2 (anatomy of a linker script), Lecture 3 (startup + vector table), Exercise 3 (blink without the SDK), and the mini-project. Skip Challenge 1 (bare-bare-metal — boot ROM straight to `main()` with no `boot2`) if you must; it returns as homework in Week 9 (Bootloaders).

---

## How to navigate this week

| File                                                                                                       | What's inside                                                                  |
|------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------|
| [README.md](./README.md)                                                                                   | This overview                                                                  |
| [resources.md](./resources.md)                                                                             | Cortex-M0+ Generic User Guide, GNU `ld` docs, RP2040 datasheet §2.2 / §2.3 / §2.8 |
| [lecture-notes/01-the-cortex-m0-memory-map.md](./lecture-notes/01-the-cortex-m0-memory-map.md)             | The fixed 4 GiB ARMv6-M map; how RP2040 fills it; flash vs SRAM vs peripherals |
| [lecture-notes/02-anatomy-of-a-linker-script.md](./lecture-notes/02-anatomy-of-a-linker-script.md)         | `MEMORY`, `SECTIONS`, VMA vs LMA, `.text`/`.data`/`.bss`, `KEEP()`, `--gc-sections` |
| [lecture-notes/03-the-startup-file-and-vector-table.md](./lecture-notes/03-the-startup-file-and-vector-table.md) | The reset handler, the vector table, `.data` copy, `.bss` zero, what runs before `main()` |
| [exercises/README.md](./exercises/README.md)                                                               | Index of exercises                                                             |
| [exercises/exercise-01-read-the-default-linker-script.md](./exercises/exercise-01-read-the-default-linker-script.md) | Read the SDK's `memmap_default.ld` line by line; annotate every directive    |
| [exercises/exercise-02-write-a-minimal-startup.md](./exercises/exercise-02-write-a-minimal-startup.md)     | Write `startup.c` + vector table; verify `.bss` is zeroed before `main()`      |
| [exercises/exercise-03-blink-without-the-SDK.md](./exercises/exercise-03-blink-without-the-SDK.md)         | Blink GP15 on Pi Pico W with a from-scratch linker + startup + `Makefile`      |
| [challenges/README.md](./challenges/README.md)                                                             | Index of challenges                                                            |
| [challenges/challenge-01-bare-bare-metal-blink.md](./challenges/challenge-01-bare-bare-metal-blink.md)     | Blink with no `boot2` — run from SRAM only, loaded via OpenOCD / probe-rs      |
| [quiz.md](./quiz.md)                                                                                       | 10 questions; datasheet + linker-script grade                                  |
| [homework.md](./homework.md)                                                                               | Six practice problems                                                          |
| [mini-project/README.md](./mini-project/README.md)                                                         | Week 3 deliverable — Pi Pico W blink with zero vendor SDK code, your linker + startup + Makefile |

---

## The Week 3 deliverable, in one line

By Sunday 23:59 local time you produce a single artifact: a public GitHub repo containing a Pi Pico W firmware that blinks GP15 at 1 Hz and prints `"crunch-wire w03 boot ok"` over UART0 at 115200 8N1 once per second — built with **zero `pico-sdk` source files in the tree**, a hand-written `pico.ld` linker script, a hand-written `startup.c` with the vector table, the official `boot2_w25q080.S` second-stage bootloader (the only file you may borrow), and a hand-written `Makefile` that calls `arm-none-eabi-gcc` and `arm-none-eabi-ld` directly with no CMake. The repo includes a `MEMORY-MAP.md` that documents every region you defined, its origin, its length, and the symbols you exported.

Week 4 of [`SYLLABUS.md`](../../SYLLABUS.md) (Modern C++ on Bare Metal) builds on this artifact: you will replace `startup.c` with `startup.cpp` and add static C++ constructors that run before `main()`. If your Week 3 startup does not call `__libc_init_array`, Week 4 will fail in a way you cannot debug without coming back here.

---

## Stretch goals

- Read the ARMv6-M Architecture Reference Manual §B1.5 (Reset behavior), pp. B1-148 to B1-152 of DDI 0419E. Annotate the four-step reset sequence the processor performs in hardware before your reset handler even gets called: (1) load `SP_main` from word 0 of the vector table, (2) load `PC` from word 1, (3) clear the `EPSR.T` bit (force Thumb mode — this is a no-op on M0+ which is Thumb-only), (4) set `LR = 0xFFFFFFFF`. Bring the annotation to Friday studio.
- Take Exercise 3 and run `arm-none-eabi-nm -n -S build/blink.elf | head -40`. Confirm `Reset_Handler` is at an address inside flash (`0x1000_xxxx`), and confirm `__isr_vector` is at exactly `0x1000_0100` (right after the 256-byte `boot2` stub).
- Modify your linker script to put `.text` at `0x2000_8000` instead of `0x1000_0100` and load the resulting `.elf` over SWD with `probe-rs run`. The firmware now runs from SRAM, not flash. Measure boot time vs the XIP build. Expected: instant, because there is no flash-read overhead — but the firmware is gone on power-cycle.
- Read someone else's linker script. Clone `STM32CubeF4` and open `STM32F407VGTx_FLASH.ld`. Spot the differences from your RP2040 script: STM32 has CCM RAM, a different flash base, ECC-protected SRAM blocks. The shape is the same, the regions differ.

---

## Up next

[Week 4 — Modern C++ on Bare Metal](../week-04/) — once your bare-metal blink is on GitHub, your reviewer has signed off on the `.map` file, and you can answer "what does the reset handler do?" in one sentence.
