# Exercise 1 — Read the Default Linker Script

**Time estimate:** ~90 minutes (most of it the first 30 lines; the second half repeats patterns).

## Problem statement

Open `pico-sdk/src/rp2_common/pico_standard_link/memmap_default.ld` and produce a line-by-line annotation of the entire file in your own words. The result is a Markdown file `notes/memmap-default-annotated.md` with the original script in fenced code blocks and your annotation between each block. This exercise builds the muscle of *reading* linker scripts — a skill you will need on every job after this week — before you start writing your own.

There is no flashing, no scope, no bench work in this exercise. It is the most important exercise of the week.

## Acceptance criteria

- [ ] A new directory `c7-week03-readld/` containing the cloned `memmap_default.ld` from a known commit of `pico-sdk` (cite the commit hash) and your `notes/memmap-default-annotated.md`.
- [ ] Every `MEMORY` region in the script is annotated with: name, base address, length, attributes, and your one-sentence explanation of what lives there. Cross-reference RP2040 datasheet §2.2 (memory map), p. 23 for each region.
- [ ] Every output section (`.boot2`, `.text`, `.rodata`, `.data`, `.bss`, `.heap`, `.stack`, plus any others the SDK declares) is annotated with: input sections matched, region (VMA), AT region (LMA, if any), and one sentence on why the section exists.
- [ ] Every `KEEP(...)` use is annotated with: what is being kept and why `--gc-sections` would otherwise remove it.
- [ ] Every `PROVIDE(...)` use is annotated with: what default value is being provided, and what would override it.
- [ ] Every symbol the script defines (`__etext`, `__data_start__`, `__bss_start__`, `end`, `_estack`, `__StackTop`, etc.) is annotated with: what consumer (the startup file? the application?) reads this symbol, and what address it resolves to in a typical build.
- [ ] A summary table at the end of your notes lists at least 10 symbols the script exports, with their typical addresses on a Pi Pico W.
- [ ] You write a 200-word reflection at the bottom: which directives surprised you, which ones you had to look up in the GNU `ld` documentation, and which one you would change if you owned this file.

## Hints

<details>
<summary>Where to find the file</summary>

Clone the SDK if you have not already:

```bash
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git log -1 --pretty=oneline src/rp2_common/pico_standard_link/memmap_default.ld
```

The file path is `src/rp2_common/pico_standard_link/memmap_default.ld`. Pin to a specific commit hash for your notes (`git log` shows the latest); the file evolves slowly but you want a stable reference.

Alternative: read it on GitHub at the same path. The web view preserves comments and is easier to scroll.
<https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_standard_link/memmap_default.ld>

</details>

<details>
<summary>Annotation template — first 40 lines worked example</summary>

Your `notes/memmap-default-annotated.md` should look like this (using the actual SDK file as input):

````markdown
# pico-sdk `memmap_default.ld` — annotated

Source: pico-sdk commit `<hash>`, file `src/rp2_common/pico_standard_link/memmap_default.ld`.
Read on `<date>`. Cross-referenced against RP2040 datasheet Sep-2024 rev, §2.2 p.23 and §2.8 p.130.

## Header

```ld
/* Based on GCC ARM embedded samples.
   Defines the following symbols for use by code:
    __exidx_start
    __exidx_end
    __etext
    __data_start__
    __preinit_array_start
    ...
    __HeapBase
    __HeapLimit
    __StackLimit
    __StackTop
    __stack (== StackTop)
*/
```

This is the file header. It lists every symbol the script will `PROVIDE` or define
unconditionally. Each of these symbols is then read by `crt0.S` (the SDK's startup
assembly file) at boot, to know where to copy from, where to copy to, where to
stop, etc. The script and the startup file are tightly coupled by this symbol list.

## MEMORY block

```ld
MEMORY
{
    FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 2048k
    RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 256k
    SCRATCH_X(rwx) : ORIGIN = 0x20040000, LENGTH = 4k
    SCRATCH_Y(rwx) : ORIGIN = 0x20041000, LENGTH = 4k
}
```

Four regions:
- `FLASH`: 2 MiB at `0x1000_0000`, read+execute. Datasheet §2.2 p.23. This is the
  XIP window into the external W25Q080 QSPI flash on the Pi Pico W.
- `RAM`: 256 KiB at `0x2000_0000`. The striped SRAM0–SRAM3 banks; datasheet §2.2 p.23.
- `SCRATCH_X`: 4 KiB at `0x2004_0000`. The SRAM4 scratchpad, intended for core-0
  stack to avoid bus contention with core-1.
- `SCRATCH_Y`: 4 KiB at `0x2004_1000`. The SRAM5 scratchpad, intended for core-1.

The two scratchpads are unused by default; they exist for multicore work (Week 8).

## SECTIONS — .boot2

```ld
SECTIONS
{
    /* Second stage bootloader is prepended to the image. It must be 256 bytes big
       and checksummed. It is usually built by the boot_stage2 target
       in the SDK */
    .flash_begin : {
        __flash_binary_start = .;
    } > FLASH

    .boot2 : {
        __boot2_start__ = .;
        KEEP (*(.boot2))
        __boot2_end__ = .;
    } > FLASH

    ASSERT(__boot2_end__ - __boot2_start__ == 256,
        "ERROR: Pico second stage bootloader must be 256 bytes in size")
}
```

Two things happening here:

1. `.flash_begin` is an empty section whose only purpose is to capture the address
   of the start of flash (`0x1000_0000`) into the symbol `__flash_binary_start`.
   This is consumed by `picotool` for the UF2 conversion (the load address).
2. `.boot2` is the 256-byte second-stage bootloader. `KEEP (*(.boot2))` prevents
   `--gc-sections` from removing it (nothing in the C code references it; the
   boot ROM jumps to it via the hardware reset). The `ASSERT` at the end refuses
   to link if `boot2` is not exactly 256 bytes — a sanity check; the boot ROM
   reads exactly 256 bytes and CRC-checks the first 252.

(...continue for the rest of the file...)
````

You are not paraphrasing the SDK comments; you are *adding* your understanding. If a SDK comment says "Defines the following symbols", your annotation explains who *consumes* those symbols and what happens if any one is missing.

</details>

<details>
<summary>Symbols to look up specifically</summary>

While reading, make sure your notes explain at least these ten symbols:

| Symbol | Where defined | Where consumed |
|--------|---------------|----------------|
| `__flash_binary_start` | `.flash_begin` section | `picotool` for UF2 load address |
| `__boot2_start__` / `__boot2_end__` | `.boot2` section | The `ASSERT` at the end of `.boot2` |
| `__etext` | end of `.text` | `crt0.S` for `.data` copy LMA |
| `__data_start__` / `__data_end__` | start/end of `.data` | `crt0.S` for `.data` copy VMA |
| `__bss_start__` / `__bss_end__` | start/end of `.bss` | `crt0.S` for `.bss` zero loop |
| `__HeapBase` / `__HeapLimit` | start/end of heap region | `_sbrk` in `newlib`'s libc, for `malloc` |
| `__StackLimit` | bottom of stack region | nothing reads it directly; sanity-check only |
| `__StackTop` | top of stack region | Vector table word 0 |
| `__binary_info_start` / `__binary_info_end` | a "binary info" range | `picotool info` to print build metadata |
| `__StackOneTop` / `__StackOneBottom` | core-1 stack | Used when `multicore_launch_core1` runs |

</details>

<details>
<summary>Comparison with `STM32F407VGTx_FLASH.ld`</summary>

If you have the time (~30 min extra), pull `STM32CubeF4` and open the equivalent
linker script:

`STM32Cube_FW_F4_V1.28.0/Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/linker/STM32F407VGTx_FLASH.ld`

Find the analogous symbols and regions. The shape is the same, the numbers differ.
Notable differences:

- STM32 puts flash at `0x0800_0000`, not `0x1000_0000`.
- STM32F407 has CCM RAM at `0x1000_0000` (different from the RP2040's flash there!).
- STM32 has no `boot2` — flash is internal, no QSPI configuration needed.
- STM32 emits a `.user_heap_stack` section explicitly with hard limits; RP2040 lets
  the stack grow freely into unused RAM.

Add a short comparison paragraph to your notes. This is the first step in
recognizing that **every Cortex-M linker script is the same script with the
numbers changed** — the discipline transfers across chips.

</details>

## What to capture

Your `notes/memmap-default-annotated.md` is the main deliverable. Plus a one-line summary at the top of your repo's `README.md`:

> "Read `pico-sdk`'s `memmap_default.ld` end to end. ~280 lines. Annotated in `notes/memmap-default-annotated.md`. The most surprising directive was `<X>`; the directive I had to look up most was `<Y>`."

This one line is what a reviewer will read first.

## Stretch goals

- Diff `memmap_default.ld` against `memmap_no_flash.ld` (also in `pico-sdk/src/rp2_common/pico_standard_link/`). The `no_flash` variant places everything in SRAM with no XIP. Document the three substantive differences in your notes. This is the variant Challenge 1 builds on.
- Run `arm-none-eabi-ld --verbose` on any of your Week 1 / Week 2 builds — this prints the *built-in* default linker script (the one used when you do not supply `-T`). Compare with `memmap_default.ld`. The built-in default targets hosted Linux, not bare-metal; spot the three big differences.
- Read `pico-sdk/src/rp2_common/pico_standard_link/crt0.S` (the assembly startup) alongside the linker script. Identify which lines in the assembly consume which symbols from the script. This is preparation for Exercise 2.

## Why this matters

You will never write a production linker script from scratch in your career. You will *read* other people's linker scripts on every embedded project you ever touch — vendor reference scripts, RTOS scripts, custom in-house scripts adapted for a new board. Linker-script literacy is one of the top three skills that distinguishes a junior embedded engineer from a senior one. The other two are reading datasheets and reading disassembly; we cover all three by Week 4.

The annotation you produce is also a public artifact. Push it to your GitHub. When a future colleague asks "what's the deal with `__etext` vs `__data_start__`?" — you have a 100-line answer ready to share.
