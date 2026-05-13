# Week 3 — Resources

Every reference here is **free** and **publicly accessible**. Page numbers cite the document revision noted; later revisions move tables but not concepts. If a register address or section number moves between revisions, re-check.

## Primary datasheets and manuals

- **ARM Cortex-M0+ Devices Generic User Guide** (ARM DUI 0662B, ~110 pages) — the core. This week is its week:
  - §2.1 (Programmer's model), pp. 2-2 to 2-6 — modes, privilege, the `xPSR` register.
  - §2.2 (Memory model), pp. 2-7 to 2-12 — the fixed 4 GiB partition: Code, SRAM, Peripheral, External RAM, External Device, System. Memory types: Normal, Device, Strongly-Ordered.
  - §2.3 (Exception model), pp. 2-13 to 2-23 — the vector table, exception entry, exception return. **Read this section twice.**
  - §2.4 (Fault handling), pp. 2-24 to 2-26 — the M0+ has only one fault: HardFault. Everything escalates.
  - §4.1 (NVIC), pp. 4-2 to 4-8 — `NVIC_ISER`, `NVIC_ICER`, `NVIC_IPR`. Used heavily in Week 7; previewed here because the vector table covers IRQ slots.
  - §4.4 (SCB), pp. 4-15 to 4-19 — the System Control Block, including `VTOR` (the vector-table offset register at `0xE000_ED08`), which lets you relocate the vector table to SRAM.
  <https://developer.arm.com/documentation/dui0662/b/>
- **ARMv6-M Architecture Reference Manual** (DDI 0419E, ~440 pages) — the instruction set + reset semantics. This week:
  - §B1.5 (Reset behavior), pp. B1-148 to B1-152 — the four-step hardware reset.
  - §B3 (System address map), pp. B3-211 to B3-216 — the canonical 4 GiB partition.
  <https://developer.arm.com/documentation/ddi0419/latest/>
- **RP2040 Datasheet** (Raspberry Pi Ltd, ~640 pages, Sep-2024 rev) — the silicon. This week:
  - §2.2 (Bus Fabric — Memory Map), pp. 23–24 — the master table of every peripheral base address. The most consulted page in the whole datasheet.
  - §2.3.2 (External Interrupts), p. 56 — the 26 RP2040-specific IRQ numbers that fill slots 16–41 of the vector table.
  - §2.6 (Power), pp. 99–104 — `POWMAN`, `WATCHDOG`. Read on first boot; we will revisit in Week 11.
  - §2.7 (Subsystem resets), pp. 105–115 — `RESETS_RESET`. We touch this in Exercise 3 to bring `IO_BANK0`, `PADS_BANK0`, and `UART0` out of reset by hand.
  - §2.8 (Boot Sequence), pp. 130–145 — the boot ROM, `boot2`, the XIP cache, the BOOTSEL+UF2 flow. **This is the section you will live in this week.**
  - §2.8.1.3 (Stage-2 bootloader), p. 132 — the 252-byte assembly stub + 4-byte CRC. The Pi Pico W uses the W25Q080 variant.
  - §4.1 (Clocks), pp. 195–238 — `XOSC`, PLLs, `clk_peri`. We assume the boot defaults (`clk_peri = 125 MHz` after the SDK boots; on a no-SDK firmware, `clk_peri` defaults to the 6 MHz XOSC unless you bring the PLL up by hand).
  - §4.2.7 (UART register table), p. 431 — same table as Week 2; reused this week for the no-SDK UART init.
  <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>
- **Raspberry Pi Pico W Datasheet** (~30 pages) — pinout on p. 4; reset circuit on p. 12; the `RUN` pin (active-low chip reset) on p. 5. The W variant differs from the original Pico only in the CYW43439 module; the RP2040 core, flash, and boot ROM are identical.
  <https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf>

## GNU `ld` and binutils

- **GNU `ld` documentation** (Binutils 2.41 reference, ~250 printed pages, HTML) — the linker. This week:
  - §3.4 (MEMORY command) — defining named physical regions.
  - §3.6 (SECTIONS command) — placing output sections; VMA, LMA, AT, `> region`, `AT > region`.
  - §3.6.5 (Output section type) — `NOLOAD` for `.bss`-like sections.
  - §3.6.8 (Output section keywords) — `KEEP()`, `PROVIDE`, `PROVIDE_HIDDEN`.
  - §3.5 (Built-in functions) — `ALIGN()`, `SIZEOF()`, `LOADADDR()`, `ABSOLUTE()`.
  <https://sourceware.org/binutils/docs/ld/>
- **GNU `as` documentation** — the assembler. You will read the `.section` and `.word` directives this week.
  <https://sourceware.org/binutils/docs/as/>
- **GCC documentation, "Specifying Subprocesses and the Switches to Pass to Them"** — for `-nostdlib`, `-nostartfiles`, `-ffreestanding`, `-fno-builtin`. These are the four flags that tell GCC you are bringing your own startup.
  <https://gcc.gnu.org/onlinedocs/gcc/Link-Options.html>

## Reference linker scripts to read

- **`pico-sdk/src/rp2_common/pico_standard_link/memmap_default.ld`** — the default RP2040 linker script. ~280 lines, well commented. Exercise 1 is reading this file end-to-end.
  <https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_standard_link/memmap_default.ld>
- **`STM32CubeF4/Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/linker/STM32F407VGTx_FLASH.ld`** — the ST reference for a different chip. Same shape, different memory regions.
  <https://github.com/STMicroelectronics/STM32CubeF4>
- **`zephyr/include/zephyr/arch/arm/cortex_m/scripts/linker.ld`** — Zephyr RTOS's generic Cortex-M linker. Heavier than yours, but illustrates how a real-world script handles initcalls, devicetree, and shared interrupts.
  <https://github.com/zephyrproject-rtos/zephyr>

## Reference startup files to read

- **`pico-sdk/src/rp2_common/pico_standard_link/crt0.S`** — the RP2040 startup file, in ARM assembly. ~200 lines. Exercise 2 reimplements this in C.
  <https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_standard_link/crt0.S>
- **`pico-sdk/src/rp2_common/boot_stage2/`** — the 256-byte boot2. Read `boot2_w25q080.S` for the W25Q080 flash chip used on the Pi Pico W.
  <https://github.com/raspberrypi/pico-sdk/tree/master/src/rp2_common/boot_stage2>
- **CMSIS startup template `startup_ARMCM0plus.c`** — ARM's reference startup file for a Cortex-M0+. ~150 lines of C.
  <https://github.com/ARM-software/CMSIS_5>

## Free books and write-ups

- **"Bare-Metal Programming for ARM"** (Daniels Umanovskis, free PDF) — different chip (Versatile Express A9), same mental model. Chapters 3 (linker script) and 4 (startup) are this week's required reading outside the lecture notes.
  <https://github.com/umanovskis/baremetal-arm>
- **"Embedded Artistry — A General Overview of What Happens Before main()"** (Phillip Johnston, free blog post) — the cleanest one-page summary of what the C runtime does before `main()`. ~3500 words.
  <https://embeddedartistry.com/blog/2019/04/08/a-general-overview-of-what-happens-before-main/>
- **"Demystifying ARM Cortex-M Memory Layout"** (Interrupt blog, Memfault) — a four-part series on `.text`, `.data`, `.bss`, the stack, and the heap, with `arm-none-eabi-nm` and `objdump` worked examples. The single best free resource on the subject.
  <https://interrupt.memfault.com/blog/how-to-write-linker-scripts-for-firmware>
- **"Linker Scripts — From Zero to Hero"** (Allen Wild, free blog post series) — three posts walking from "no linker script" to a fully fledged Cortex-M script. Uses the STM32L4 as the target; the discipline is identical to the RP2040.
- **"What Happens When You Press Reset?"** (Adam Heinrich, free blog post) — covers the boot ROM → boot2 → main() chain on the RP2040 specifically, with `objdump` snippets.

## Videos (free)

- **"Modern Embedded Systems Programming Course" — Quantum Leaps / Miro Samek**, lessons 10–14: linker scripts, startup files, the C runtime. STM32 target, but the discipline transfers exactly. ~60 minutes total across the four lessons.
  <https://www.state-machine.com/quickstart>
- **"Embedded Systems — Shape The World" (Jonathan Valvano, UT Austin)** — chapter on the memory map and the linker. Free on edX.
- **"Bare Metal Embedded — Series" (LowLevel-Learning on YouTube)** — short, focused videos on startup files, vector tables, and reset handlers. Good for visual reinforcement of the lecture material.

## Tools you will use this week

| Command | What it does |
|---------|--------------|
| `arm-none-eabi-gcc -c -mcpu=cortex-m0plus -mthumb -ffreestanding -nostdlib startup.c` | Compile a startup file with no implicit libc startup |
| `arm-none-eabi-ld -T pico.ld -o blink.elf startup.o main.o boot2.o -Map=blink.map` | Link with your own script; emit a map file |
| `arm-none-eabi-objdump -d build/blink.elf` | Disassemble — find your `Reset_Handler`, find the vector table |
| `arm-none-eabi-objdump -h build/blink.elf` | Section headers — confirm `.text` at `0x1000_0000`, `.data` LMA in flash / VMA in SRAM |
| `arm-none-eabi-readelf -l build/blink.elf` | Program headers — what gets loaded at boot, where |
| `arm-none-eabi-nm -n -S build/blink.elf` | Symbols, sorted by address, with sizes — find `__isr_vector`, `Reset_Handler`, `_estack` |
| `arm-none-eabi-size build/blink.elf` | One-line summary: `text data bss dec hex filename` |
| `elf2uf2 build/blink.elf build/blink.uf2` | Convert ELF to UF2 for BOOTSEL drag-and-drop. Ships with `pico-sdk`. |
| `picotool info -a build/blink.uf2` | Inspect a `.uf2` — confirms the load address and the boot2 CRC |
| `picotool load -f build/blink.uf2` | Flash a `.uf2` over USB to a Pico in BOOTSEL mode |
| `picotool load -f build/blink.elf -t elf` | Flash an `.elf` directly (no UF2 conversion needed in `picotool` ≥ 1.1.2) |
| `openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "program build/blink.elf verify reset exit"` | Flash via SWD — needed for SRAM-only loads in Challenge 1 |

## RP2040 memory map cheat-sheet (Sep-2024 rev, §2.2 p. 23)

The single most consulted page in the RP2040 datasheet. Mark it.

| Region | Base | Length | Type | Notes |
|--------|------|------:|------|-------|
| ROM | `0x0000_0000` | 16 KiB | Read-only, executable | Boot ROM; jumps to `boot2` |
| XIP cache + XIP flash | `0x1000_0000` | 16 MiB window (2 MiB physical) | Read-only, executable | Where your `.text` lives |
| XIP_NOALLOC | `0x1100_0000` | 16 MiB window | Read-only, executable | XIP without cache allocation |
| XIP_NOCACHE | `0x1300_0000` | 16 MiB window | Read-only, executable | XIP bypassing the cache (slower) |
| XIP_NOCACHE_NOALLOC | `0x1500_0000` | 16 MiB window | Read-only, executable | XIP bypassing cache, no allocation |
| SRAM0–SRAM3 (striped) | `0x2000_0000` | 256 KiB | Read-write, executable | Main 256 KiB SRAM bank; striped across 4 sub-banks for bus bandwidth |
| SRAM4 | `0x2004_0000` | 4 KiB | Read-write, executable | Core 0 stack scratchpad |
| SRAM5 | `0x2004_1000` | 4 KiB | Read-write, executable | Core 1 stack scratchpad |
| APB peripherals (UART, SPI, I2C, PWM, ADC, …) | `0x4000_0000` | depends | Device | AHB-Lite peripheral fabric |
| IO_BANK0, PADS_BANK0 | `0x4001_4000`, `0x4001_c000` | 4 KiB each | Device | GPIO mux + pad control (Week 2) |
| AHB peripherals (DMA, USB, PIO, …) | `0x5000_0000` | depends | Device | Higher-bandwidth peripherals |
| SIO | `0xd000_0000` | 4 KiB | Device, single-cycle | Core-local I/O bus (Week 2) |
| PPB (NVIC, SCB, SysTick) | `0xe000_e000` | 4 KiB | Device | Cortex-M0+ System Control Space |

## Linker-script syntax cheat-sheet

You will refer to these often. Mark them.

| Directive | Purpose | Example |
|-----------|---------|---------|
| `MEMORY { … }` | Declare named physical regions | `FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 2M` |
| `SECTIONS { … }` | Declare output sections | `.text : { *(.text*) } > FLASH` |
| `> region` | Place section's VMA in region | `.bss : { *(.bss*) } > RAM` |
| `AT > region` | Place section's LMA in region | `.data : { *(.data*) } > RAM AT > FLASH` |
| `.` (location counter) | The current address as sections are emitted | `_etext = .;` |
| `ALIGN(n)` | Round `.` up to a multiple of n | `. = ALIGN(4);` |
| `KEEP(…)` | Prevent `--gc-sections` from removing | `KEEP(*(.isr_vector))` |
| `PROVIDE(sym = expr)` | Define a symbol unless user already did | `PROVIDE(_estack = ORIGIN(RAM) + LENGTH(RAM));` |
| `ENTRY(symbol)` | The entry-point symbol (informational) | `ENTRY(Reset_Handler)` |
| `LOADADDR(.data)` | The LMA of an output section | `_sidata = LOADADDR(.data);` |
| `*(.text*)` | Wildcard input section match | `*(.text*) *(.rodata*)` |
| `NOLOAD` | Section has no bytes in the file | `.bss (NOLOAD) : { … } > RAM` |

## Glossary

| Term | One-line definition |
|------|---------------------|
| **VMA** | Virtual Memory Address — where the symbol lives at runtime |
| **LMA** | Load Memory Address — where the linker placed the initial value in the binary image |
| **`.text`** | Read-only code + constants, in flash |
| **`.data`** | Initialized read-write globals, VMA in SRAM, LMA in flash, copied at boot |
| **`.bss`** | Zero-initialized read-write globals, in SRAM, zeroed at boot |
| **`.rodata`** | Read-only constants. Usually merged into `.text` on Cortex-M. |
| **`.stack`** | The descending stack region; usually the top of SRAM |
| **`.heap`** | Optional, between `.bss` and stack, for `malloc`. Most embedded firmware skips this. |
| **Vector table** | The contiguous 48-entry array of function pointers at the start of `.text`; first entry is the initial SP, second is the reset handler |
| **`boot2`** | The 256-byte second-stage bootloader on RP2040; configures QSPI XIP. CRC-checked by ROM. |
| **`Reset_Handler`** | The function called by the boot ROM after `boot2` returns; sets up `.data`/`.bss`, calls `main` |
| **`__isr_vector`** | Conventional symbol name for the vector table |
| **`_estack`** | Conventional symbol name for the top-of-stack address (initial SP) |
| **`__libc_init_array`** | The libc function that runs static C++ constructors before `main()` |
| **XIP** | eXecute-In-Place — running code directly from flash without copying to SRAM first |
| **`VTOR`** | Vector Table Offset Register, at `0xE000_ED08`. Default 0; set to relocate the table. |
