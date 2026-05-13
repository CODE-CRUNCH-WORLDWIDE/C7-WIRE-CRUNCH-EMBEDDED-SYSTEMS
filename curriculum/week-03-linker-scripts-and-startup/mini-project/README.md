# Mini-Project — The From-Scratch Pi Pico W Blink

> Build a Pi Pico W firmware that blinks GP15 at 1 Hz and prints a structured UART log over UART0 at 115200 8N1 — using your own linker script, your own startup file, your own `Makefile`, and **zero `pico-sdk` source files in the tree**. Then publish the **memory-map artifact** — the one-page truth-telling document about every region, every section, and every symbol in your build, in the C7 voice. Week 4 of [`SYLLABUS.md`](../../../SYLLABUS.md) references this file by name.

This is the practical synthesis of Week 3. It combines Exercise 1 (read a linker script), Exercise 2 (write a startup), and Exercise 3 (blink without the SDK) into a single firmware image, then asks you to write the kind of memory-map documentation a senior firmware engineer writes when bringing up a new chip on a new board. The memory-map artifact is the Week 3 brand signature, analogous to the Week 1 bring-up note and the Week 2 register table.

**Estimated time:** 6 hours, spread across Wednesday – Saturday.

---

## What you will build

A C firmware for the Raspberry Pi Pico W that:

1. **Blinks GP15** at exactly **1.0 Hz ± 1%**, observed for at least 60 seconds on a scope or logic analyzer.
2. **Emits a UART log** over UART0 at **115200 8N1** (GP0 = TX, GP1 = RX) once per second, with the format:

   ```
   crunch-wire w03 t=<ms> boot_counter=0x<hex> sram_free=<bytes>
   ```

   For example: `crunch-wire w03 t=00012345 boot_counter=0xdeadbeef sram_free=261952\r\n`.

3. **Reads three globals at startup** and reports their values in the log line, to prove the C runtime works:
   - `boot_counter` — initialized in `.data` to `0xdeadbeef`. Confirms the `.data` copy ran.
   - `event_counter` — uninitialized, in `.bss`. Increments once per log line. Confirms `.bss` was zeroed at boot.
   - `magic` — a `static const uint32_t magic = 0xc7c7c7c7;` in `.rodata`. Confirms the constant is reachable.

4. **Builds with no `pico-sdk` source files in the tree**, only:
   - Your `pico.ld` (linker script)
   - Your `startup.c` (vector table + reset handler)
   - Your `main.c` (the blink + UART + log loop)
   - Borrowed `boot2_w25q080.S` from `pico-sdk` (the only file you may copy; cite the source path + commit hash)
   - Borrowed `pad_checksum.py` from `pico-sdk` (for the boot2 CRC)
   - Your `Makefile` (no CMake)

5. **Survives a 30-minute soak** of continuous operation, with no missed log lines.

And a **memory-map artifact** (`MEMORY-MAP.md`) at the repo root that documents:
- Every region in your linker script.
- Every output section, with VMA, LMA, size, and purpose.
- Every symbol your linker script exports, with the address it resolves to on a typical build and the consumer (startup file? `Reset_Handler`? `main()`?).
- The full output of `arm-none-eabi-size build/blink.elf` and `arm-none-eabi-objdump -h build/blink.elf`.

---

## Acceptance criteria

- [ ] A new public GitHub repo `c7-week03-bare-blink-<yourhandle>`.
- [ ] `git clone …` and `make` succeeds with no warnings (other than the ARM assembler's note about `boot2_w25q080.S`).
- [ ] `build/blink.uf2` exists and is ≤ 4 KiB.
- [ ] Flashed via BOOTSEL drag-and-drop, the board demonstrates all four behaviors above (blink, UART log, three-global proof, no SDK).
- [ ] `find . -name '*.c' -o -name '*.h' | xargs grep -l 'pico/stdlib\|hardware_uart\|hardware_gpio\|pico_sdk'` returns **zero** matches.
- [ ] `MEMORY-MAP.md` at the repo root contains every required section (see template).
- [ ] `README.md` at the repo root contains setup, build, flash, wiring diagram (ASCII or image), and a "What works / What does not" header.
- [ ] A `traces/` directory contains at least:
  - One scope or Saleae screenshot of GP15 showing the 1 Hz toggle.
  - One Saleae capture of UART TX (GP0) with the **Async Serial 115200 8N1** decoder applied, showing at least two complete log lines.
- [ ] A **fault model card** in `MEMORY-MAP.md` (template below) covers at least three faults.

---

## The memory-map artifact (required)

Your `MEMORY-MAP.md` follows this exact structure, in the C7 voice:

```
# Week 3 Memory Map — <yourhandle>

> Pi Pico W, RP2040, gcc-arm-none-eabi <version>, GNU ld <version>
> Built on <date> on <macOS Sonoma 14.5 / Ubuntu 22.04 / WSL2 Ubuntu 22.04>
> Tagline: one-sentence status — e.g. "All four behaviors work; build is 1.2 KiB
> of .text, .data of 8 bytes, .bss of 32 bytes; .data LMA = 0x100012ec verified
> on objdump -h; UART log line cadence is 1.00 s ± 5 ms on scope."

## MEMORY regions

| Region | Origin | Length | Attributes | Source | Purpose |
|--------|--------|-------:|------------|--------|---------|
| FLASH  | 0x10000000 | 2 MiB | (rx) | RP2040 §2.2 p.23 | XIP window into W25Q080 QSPI |
| RAM    | 0x20000000 | 256 KiB | (xrw) | RP2040 §2.2 p.23 | Striped SRAM0..SRAM3 |

## SECTIONS — output layout

| Section | VMA | LMA | Size | Source | Purpose |
|---------|----:|----:|-----:|--------|---------|
| .boot2 | 0x10000000 | 0x10000000 | 256 B | boot2_w25q080.S | 2nd-stage bootloader, CRC-checked by boot ROM |
| .isr_vector | 0x10000100 | 0x10000100 | 192 B | startup.c | 48-entry Cortex-M0+ vector table |
| .text | 0x100001c0 | 0x100001c0 | ~1.0 KiB | all C objects | Code + .rodata |
| .data | 0x20000000 | 0x100012ec | 8 B | main.c | Initialized globals; copied by Reset_Handler |
| .bss | 0x20000008 | (none) | 32 B | main.c | Zero-initialized globals; zeroed by Reset_Handler |
| (stack) | 0x20040000 ↓ | — | grows down | runtime | Initial SP = _estack |

## Symbols exported by pico.ld

| Symbol | Address | Defined in script | Consumed by | Purpose |
|--------|--------:|-------------------|-------------|---------|
| _estack | 0x20040000 | `_estack = ORIGIN(RAM) + LENGTH(RAM);` | __isr_vector[0] | Initial Main Stack Pointer |
| __isr_vector | 0x10000100 | section `.isr_vector` | Boot ROM (via VTOR) | Start of vector table |
| Reset_Handler | 0x100001c0 | startup.c | Vector table word 1 | First C function called |
| _sidata | 0x100012ec | `_sidata = LOADADDR(.data);` | Reset_Handler | LMA of .data |
| _sdata, _edata | 0x20000000, 0x20000008 | section `.data` | Reset_Handler | VMA range of .data |
| _sbss, _ebss | 0x20000008, 0x20000028 | section `.bss` | Reset_Handler | Range of .bss to zero |

Minimum 8 rows. Every row identifies the script line that defines the symbol
and the file that consumes it.

## Size summary

  $ arm-none-eabi-size build/blink.elf
     text    data     bss     dec     hex filename
     1056      16      32    1104     450 build/blink.elf

  total flash use: ~1.4 KiB (text + data LMA + boot2)
  total ram use:   ~40 bytes (data + bss) + 4 KiB stack budget
  free ram:        ~257 KiB available for application growth

## Section headers (objdump -h)

  $ arm-none-eabi-objdump -h build/blink.elf
  [paste the actual output]

## The three-global proof

The firmware reads three globals on every boot and reports their values
over UART, to prove the C runtime works:

  - `boot_counter` (initialized in .data to 0xdeadbeef) — should read
    0xdeadbeef. If it reads 0x00000000, your .data copy did not run.
  - `event_counter` (uninitialized, in .bss) — should read as 0 at boot
    and increment every log line. If it reads a nonzero garbage value at
    boot, your .bss zero did not run.
  - `magic` (`static const uint32_t magic = 0xc7c7c7c7;` in .rodata) —
    should read 0xc7c7c7c7. If it reads something else, your .rodata is
    misplaced.

  Observed at boot: boot_counter=0xdeadbeef, event_counter=0, magic=0xc7c7c7c7.
  All three correct. C runtime is healthy.

## What works

  - 1 Hz blink: measured period 1.000 s ± 5 ms on scope across a 60 s window.
  - UART log: 1 line/sec, 115200 8N1, decoded cleanly on Saleae over a
    100-line capture.
  - All three globals correct on every boot of a 30-min soak.
  - No SDK files in src tree; `grep` confirms.

## What does not work (or is not yet measured)

  - Long-term drift of the 1 Hz blink: I have not measured drift over
    > 60 seconds. The clock source is the boot-default XOSC at 12 MHz,
    which has ±50 ppm spec — over 60 s that is up to 3 ms of drift,
    which is within my 5 ms tolerance, but I cannot rule out longer-term
    accumulation.
  - HardFault handler: it traps with bkpt #0. I have not tested that a
    real HardFault (e.g. unaligned access) is actually trapped — only that
    Default_Handler is wired in the vector table.
  - PLL: I am running clk_sys at the boot-default 12 MHz. This means
    UART divisor IBRD/FBRD = 6/33, not the 67/52 you would compute for
    125 MHz. This is documented but not "ideal."
  - Be honest. The reader's trust scales with what you admit.

## Fault model

(see template below — at least three rows)

## Bench artifacts

  traces/gp15-1hz.png         scope/Saleae screenshot of GP15 at 1 Hz
  traces/uart-decode.sal      Saleae capture, UART decoded, ≥ 2 log lines
  notes/memory-map-from-memory.md   Problem 1 from homework
  notes/linker-script-deep-walk.md  Problem 2
  notes/Makefile.handwritten        Problem 3
  notes/vector-table-walkthrough.md Problem 4
  notes/intentional-breaks.md       Problem 5
  notes/week-03-reflection.md       Problem 6

## Open questions

  One or two paragraphs of "I don't yet understand why X." Example:
  "My Reset_Handler's .data copy loop seems to be running even though
  the loop bound `_edata - _sdata` is only 8 bytes. I would expect this
  to be a single iteration but objdump suggests the compiler unrolled it
  to two iterations. I have not investigated why."

## Toolchain pinning

  arm-none-eabi-gcc:  13.2.Rel1
  arm-none-eabi-ld:   2.41
  picotool:           1.1.x
  python (for pad_checksum.py): 3.11
  Make:               GNU make 4.3
  Borrowed boot2_w25q080.S: pico-sdk commit <hash>, path src/rp2_common/boot_stage2/
  Borrowed pad_checksum.py: pico-sdk commit <hash>, path src/rp2_common/boot_stage2/
```

---

## The fault model card

Every C7 deliverable from Week 1 onward includes one of these. The format from Weeks 1 and 2 carries through:

```
┌─────────────────────────────────────────────────────────────────────┐
│  FAULT MODEL — c7-week03-bare-blink-<yourhandle>                    │
│                                                                     │
│  .data copy fails (linker symbols wrong):  boot_counter reads 0;    │
│                                            detectable from UART log │
│                                                                     │
│  .bss zero fails (loop bound wrong):       event_counter reads      │
│                                            garbage; detectable from │
│                                            UART log on first boot   │
│                                                                     │
│  boot2 CRC fails (build issue):            board drops into BOOTSEL;│
│                                            no firmware runs;        │
│                                            recoverable by re-flash  │
│                                                                     │
│  Vector table garbage-collected:           HardFault loop at boot;  │
│                                            no UART output; visible  │
│                                            only via SWD or "no LED" │
│                                                                     │
│  Stack overflow (too deep call chain):     stack collides with .bss;│
│                                            ASSERT in linker script  │
│                                            catches the worst case   │
│                                                                     │
│  clk_peri assumption wrong (12 vs 125 MHz):UART output is garbage   │
│                                            characters; visible on   │
│                                            Saleae as wrong bit rate │
└─────────────────────────────────────────────────────────────────────┘
```

Minimum three rows. Each row names a fault and the detection/mitigation. "No mitigation; out of scope for Week 3" is an acceptable answer for one row — be explicit.

---

## Suggested file layout

```
c7-week03-bare-blink-<yourhandle>/
├── README.md
├── MEMORY-MAP.md
├── Makefile
├── pico.ld
├── startup.c
├── main.c
├── boot2_w25q080.S         ← borrowed from pico-sdk (commit hash in comment)
├── pad_checksum.py         ← borrowed from pico-sdk (commit hash in comment)
├── build/                  ← .gitignored; outputs of `make`
└── traces/
    ├── gp15-1hz.png
    └── uart-decode.sal
```

You can keep all the C in `startup.c` + `main.c`. ~200 lines of C total. The Makefile is ~60 lines. The linker script is ~80 lines. The vector table is ~50 lines of C. Total project: under 400 lines of code you wrote yourself, plus the borrowed boot2.

---

## Suggested order of operations

### Phase 1 — Build skeleton (1 h)

1. Make repo, copy your Exercise 3 directory as the starting point.
2. Confirm `make` produces a working `.uf2` and a flashable blink.
3. Commit a "behaviors-restored" baseline.

### Phase 2 — Add the three-global proof (45 min)

- Add `boot_counter`, `event_counter`, and `magic` to `main.c`.
- Modify the UART log line to include their values.
- Verify on a terminal that the values are correct at boot.

### Phase 3 — The 30-minute soak (30 min wall, mostly wait)

- Start the firmware.
- Open a terminal that times-tamps each log line (`screen` does not; `picocom --logfile=soak.log` does, with `--noinit` and your own timestamp wrapper).
- Walk away for 30 minutes.
- Come back. Check that the log has ~1800 lines, monotonically increasing in `t=` and `event_counter`.

### Phase 4 — Bench validation (1 h)

- Scope screenshot of GP15. Mark the 1.000 s period.
- Saleae capture of UART TX with Async Serial 115200 8N1 decoded. Two complete log lines visible.

### Phase 5 — The memory-map artifact (2 h)

- Write `MEMORY-MAP.md` following the structure above.
- Cite every section.
- Include the fault-model card. Minimum three rows.
- Be ruthless about "what does not work or is not yet measured."

### Phase 6 — Polish (45 min)

- Write the `README.md`. Setup, build, flash, wiring diagram. Link to `MEMORY-MAP.md`.
- Confirm the repo builds from a fresh clone (test in a `git clone` to `/tmp`).
- Commit traces.
- Push.

---

## Rubric

| Criterion | Weight | "Great" looks like |
|-----------|------:|--------------------|
| It builds | 15% | `git clone … && cd … && make` works on a fresh clone with no edits |
| All four behaviors work on bench | 20% | Reviewer flashes your `.uf2`, sees the log, sees the blink, the three globals match expectations |
| No SDK in tree | 15% | `grep -l 'pico_sdk\|hardware_uart\|hardware_gpio' src/*.c src/*.h` returns nothing |
| Memory-map quality | 25% | Datasheet-grade voice; addresses cited; ≥ 6 region/section rows; ≥ 6 symbol rows; honest "what does not work" section; ≥ 3 fault-model rows |
| Bench artifacts | 15% | `.sal` files decode cleanly; screenshots show the actual data, not just a sketch |
| README + repo hygiene | 5% | A reviewer with no context can flash and run within 15 minutes |
| Code readability | 5% | One job per function; ≤ 30 lines per function; every magic number cites a datasheet page |

---

## Stretch goals

- **Bring up `PLL_SYS` to 125 MHz in `main()` before the UART init.** Read RP2040 datasheet §2.18 (PLL), p. 234, and §2.15.7 (CLK_SYS_CTRL), p. 217. The sequence: enable XOSC, configure PLL_SYS reference + feedback divisors, wait for PLL lock, set `clk_sys` source to PLL_SYS, recompute the UART divisor for `clk_peri = 125 MHz`. ~50 lines of register writes. Document in `notes/pll-bringup.md`. The resulting firmware runs ~10× faster.
- **Add a HardFault handler that records the stacked PC.** When the CPU faults, the hardware pushes `R0–R3, R12, LR, PC, xPSR` onto the stack. A custom `HardFault_Handler` (replacing the default alias) can read the stacked PC, write it to a known SRAM address (say, `0x2003_fff0`), and reset the chip via `AIRCR.SYSRESETREQ`. On the next boot, the firmware reads that address and prints "previous boot HardFaulted at PC=0x…\r\n" over UART. ~30 lines of assembly + C. This is the kernel of a fault recorder; Week 7 makes it production-grade.
- **Replace the `delay_loop` with `SysTick`-based delays.** SysTick is a 24-bit countdown timer in the Cortex-M0+ system control space, at `0xe000_e010`. Configure it for 1 ms ticks via `SYST_RVR` (the reload value). Implement `delay_ms(uint32_t ms)`. This is much more accurate than a calibrated busy loop and is the standard idiom for sub-RTOS timing.
- **Port the same firmware to an STM32F4 Discovery board.** Read RM0090 §2.3.1, p. 51 (memory map), and produce an `stm32f4.ld` and `stm32f4_startup.c`. The diff vs the RP2040 version is ~30 lines of script and ~10 lines of C. Document the diff in `notes/stm32-port.md`. This is the exercise that makes the architecture-level skills click into place.
- **Port the same firmware to Rust + `cortex-m-rt`.** No linker script (the crate provides one); no startup file (the crate provides it); the application is ~30 lines of Rust. Compare the binary size with your C version. Expected: Rust ~3 KiB vs C ~1.5 KiB, all overhead from the runtime crates. Document.

---

## Why this matters

This is the artifact a future hiring manager will ask you to show when they want to know "do you understand the C runtime on bare metal?" The C7 memory-map specifically is the brand signature for Week 3, analogous to the Week 1 bring-up note and the Week 2 register table. Week 4 of the syllabus (Modern C++ on Bare Metal) builds on the assumption that you can write a Cortex-M0+ startup file in your sleep.

The no-SDK requirement is deliberate: a senior firmware engineer must be able to bring up a new chip on a new board *without* a vendor SDK — for chips that have no SDK (early silicon), for products that need a smaller-than-SDK footprint (sub-2 KiB firmware), or for portability across vendors (the same blink runs on RP2040 and STM32F4 with a 30-line linker-script diff). By Sunday, the answer to "can you write a linker script?" should be a yes, with a working `.uf2` to back it up.

A C7 graduate at Week 3 can author a linker script and a startup file for any Cortex-M target, given the part's datasheet, in 30 minutes. By Week 12 you will do this for an unfamiliar Cortex-M board you have never seen. The muscle starts here.

---

## Submission

Commit. Push. Open a PR in the cohort review tracker linking to your repo and to your `MEMORY-MAP.md`. A peer or TA will sign off on:

1. The repo builds.
2. No SDK files in the tree (the `grep` check passes).
3. The memory map cites at least 6 sections and at least 6 symbols.
4. The three-global proof confirms on the UART log.
5. The fault-model card has at least three rows.

Once signed off, you are cleared for Week 4.
