# Lecture 2 — Anatomy of a Linker Script

> **Outcome:** You can read a GNU `ld` linker script line by line and explain what every directive does. You can author one from scratch for the RP2040, ~60 lines, that places `.text` in flash, `.data` with VMA in SRAM and LMA in flash, `.bss` in SRAM, and exports the symbols `_estack`, `_sidata`, `_sdata`, `_edata`, `_sbss`, `_ebss` that your startup file will consume in Lecture 3.

---

## 1. What `ld` actually does

`arm-none-eabi-ld` is the GNU linker. Its job in three sentences:

1. Take a set of object files (`.o`) and possibly archive files (`.a`), each containing **input sections** like `.text.foo`, `.data.bar`, `.bss.baz`.
2. Decide which **output section** each input section belongs to (e.g. all `.text.*` get merged into one output `.text` section, all `.data.*` into `.data`, etc.).
3. Assign each output section a **VMA** (where it appears at runtime) and an **LMA** (where its bytes live in the binary image), then write the final `.elf` file with those addresses baked in.

Without a linker script, `ld` uses its built-in defaults — which assume a hosted Linux process with shared libraries, a stack on a separate page, and the loader sorting out addresses at runtime. On bare metal we have no loader. We have a flash chip and an SRAM bank. The linker has to know which goes where, and that is what the linker script tells it.

The script is a plain text file, conventionally `*.ld`. You point `ld` at it with `-T script.ld`. The syntax is its own little language — closer to `awk` than to C, but readable. The full spec is in the GNU `ld` manual (§3, "Linker Scripts"). We will cover the 90% subset every embedded engineer uses.

---

## 2. The two top-level commands you cannot skip

A working linker script for a Cortex-M part needs exactly two top-level commands:

```
MEMORY { … }    /* Where the physical memory regions are. */
SECTIONS { … }  /* What output sections exist and which region they live in. */
```

Everything else — `ENTRY`, `PROVIDE`, `OUTPUT_FORMAT`, `INCLUDE` — is optional. The two commands above are not.

### 2.1 `MEMORY`

The `MEMORY` command declares the named physical regions of the target. Syntax:

```
MEMORY {
    name (attributes) : ORIGIN = base, LENGTH = size
    …
}
```

For the RP2040 the canonical declaration is:

```
MEMORY {
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 2M
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 256K
}
```

Three things to notice:

- **Attributes in parentheses.** `r` = readable, `w` = writable, `x` = executable. `(rx)` means "read-only, executable" — that is your flash. `(xrw)` means "executable, readable, writable" — that is your SRAM. The attributes are informational only; `ld` does not enforce them at link time. They are documentation for the reader.
- **`ORIGIN` is the base address.** The first byte of `FLASH` is at `0x1000_0000`. The first byte of `RAM` is at `0x2000_0000`. These are *byte* addresses; the linker counts in bytes.
- **`LENGTH` is the size.** `2M` = 2 × 1024 × 1024 = 2 MiB = `0x0020_0000` bytes. `256K` = 256 × 1024 = 256 KiB = `0x0004_0000` bytes. The suffixes `K`, `M`, `G` are powers of 1024 in `ld`-syntax (different from SI). The linker will refuse to emit a section that does not fit in its region.

You can declare more regions if you have them. An STM32F407 script declares `FLASH`, `RAM`, `SRAM2`, and `CCM`. An STM32H7 declares `FLASH`, `DTCMRAM`, `RAM_D1`, `RAM_D2`, `RAM_D3`. The RP2040 has one flash and one (effective) RAM, so two regions is enough.

### 2.2 `SECTIONS`

The `SECTIONS` command is where the work happens. It declares the **output sections** in the order the linker should emit them, and for each one, which **input sections** to gather and which **region** to place it in. Syntax (sketch):

```
SECTIONS {
    .text : {
        <input-section selectors>
    } > FLASH

    .data : {
        <input-section selectors>
    } > RAM AT > FLASH

    .bss (NOLOAD) : {
        <input-section selectors>
    } > RAM
}
```

The colon after the output-section name is required. The braces enclose **input-section selectors** — wildcards like `*(.text*)` that match input sections from any object file whose name begins with `.text`. The trailing `> FLASH` or `> RAM` chooses the region.

The `AT > FLASH` is the magic for `.data`: it tells the linker to set the section's **VMA** in `RAM` (the unmarked `> RAM`) but its **LMA** in `FLASH`. We will dwell on this in §4.

---

## 3. A minimal working script for the RP2040, line by line

Here is a ~60-line linker script that boots a Pi Pico W. Read every line; in §4–§9 we annotate each one.

```
/* C7 · Crunch Wire — Week 03 — pico.ld
 *
 * Minimal RP2040 linker script. Targets the Pi Pico W.
 * Citations: GNU ld §3.4 (MEMORY), §3.6 (SECTIONS); RP2040 datasheet §2.2 (memory map).
 */

/* The entry-point symbol. Informational; the boot ROM uses the vector
 * table to find the reset handler, not this symbol. But it shows up in
 * the .map file and in `arm-none-eabi-readelf -h`. */
ENTRY(Reset_Handler)

MEMORY {
    /* 2 MiB external QSPI flash, RP2040 datasheet §2.2 p.23 */
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 2M
    /* 256 KiB SRAM (striped SRAM0..SRAM3) */
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 256K
}

/* Initial stack pointer = top of SRAM. The stack grows down. */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* Optional heap size; 0 means "no heap". Most bare-metal firmware skips malloc. */
_heap_size = 0;

SECTIONS {

    /* The 256-byte second-stage bootloader. Must be the first 256 bytes
     * of flash. The boot ROM verifies its CRC at boot. RP2040 §2.8.1 p.132. */
    .boot2 : {
        KEEP(*(.boot2))
        . = ALIGN(256);          /* Pad to 256 bytes if shorter. */
    } > FLASH

    /* The vector table. ARMv6-M requires 32-bit alignment; we use 256-byte
     * alignment to satisfy VTOR's alignment rule (which is 32 vectors * 4
     * bytes = 128, rounded up to 256). Cortex-M0+ UG §4.4.4 (VTOR). */
    .isr_vector : ALIGN(256) {
        __isr_vector = .;
        KEEP(*(.isr_vector))     /* The vector-table input section. */
        KEEP(*(.isr_vector.*))
    } > FLASH

    /* Code and read-only data. */
    .text : {
        *(.text*)                /* All code. */
        *(.rodata*)              /* All read-only constants. */
        *(.glue_7)               /* ARM-Thumb glue (legacy). */
        *(.glue_7t)
        *(.eh_frame)             /* Unwind tables. We do not use them. */
        KEEP(*(.init))           /* libc init code, if linked. */
        KEEP(*(.fini))           /* libc fini code, if linked. */
        . = ALIGN(4);
        _etext = .;              /* End of .text — start of .data's LMA. */
    } > FLASH

    /* C++ constructor / destructor arrays. We do not use them this week,
     * but Week 4 (C++ on bare metal) requires them. Define the symbols
     * unconditionally so __libc_init_array can find them. */
    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH

    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    /* The ARM-specific unwind tables. Required by GCC even if you do not
     * use exceptions, to satisfy the linker; we put them in FLASH. */
    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH

    /* Initialized data: VMA in RAM (where the program sees it),
     * LMA in FLASH (where the initial values live in the image).
     * The Reset_Handler copies LMA -> VMA at boot. */
    _sidata = LOADADDR(.data);   /* "source initialized data" = LMA */

    .data : {
        . = ALIGN(4);
        _sdata = .;              /* start of .data in RAM */
        *(.data*)
        *(.ramfunc*)             /* functions explicitly placed in RAM */
        . = ALIGN(4);
        _edata = .;              /* end of .data in RAM */
    } > RAM AT > FLASH

    /* Zero-initialized data. NOLOAD: no bytes in the .elf image,
     * the Reset_Handler zeroes this region at boot. */
    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;   /* newlib's preferred name */
        *(.bss*)
        *(COMMON)                /* uninitialized globals from C */
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > RAM

    /* The heap, if any. _heap_size is defined at the top of this file. */
    ._user_heap (NOLOAD) : {
        . = ALIGN(8);
        _heap_start = .;
        . = . + _heap_size;
        . = ALIGN(8);
        _heap_end = .;
    } > RAM

    /* End of RAM use; the stack grows downward from _estack into the
     * remaining unused RAM. Check that we have not run out. */
    . = ALIGN(4);
    _end_of_static_data = .;

    /* Stack-overflow check: ensure at least 4 KiB of stack space. */
    ASSERT(_estack - _end_of_static_data >= 0x1000,
           "Less than 4 KiB of stack space available")

    /* Strip debug-only sections that would otherwise inflate the .elf. */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}
```

That is ~95 lines including comments. The 60-line version drops all the C++-constructor `.init_array` machinery and the `.ARM.exidx`/`/DISCARD/` cleanup; the rest is essential.

We now walk every concept in detail.

---

## 4. VMA vs LMA — the most-confused topic in linker scripts

Take an initialized global in C:

```c
uint32_t boot_counter = 42;
```

This global has *two* addresses, not one:

- The **VMA** (Virtual Memory Address): where the program *sees* the variable at runtime. After the C runtime starts, `&boot_counter` evaluates to this address. The CPU loads from and stores to this address. It must be writable, so it lives in SRAM. Typical value: `0x2000_0000` (or some offset into SRAM).
- The **LMA** (Load Memory Address): where the *initial value* `42` lives in the binary image on flash. When the `.uf2` is loaded, the byte sequence `0x2a 0x00 0x00 0x00` (little-endian 42) is written into flash at this address. Typical value: `0x1000_0420` (or some offset into flash, right after `.text`).

The runtime never reads from the LMA. The runtime only ever reads from and writes to the VMA. The LMA exists for exactly one purpose: the reset handler's job, on every boot, is to copy the bytes from `[LMA, LMA + size)` to `[VMA, VMA + size)` before `main()` runs. After that copy, the LMA is dead memory — `0x1000_0420` is never touched again until the next power-cycle.

This split exists because:
- The runtime initial value (`42`) needs to survive a power-cycle, so it must live in non-volatile memory (flash).
- The runtime mutable copy needs to be writable at single-instruction speed, which flash is not (a write to flash takes ~1 ms and requires programming the QSPI controller), so the runtime copy must live in SRAM.

The linker-script syntax is:

```
.data : {
    *(.data*)
} > RAM AT > FLASH
```

`> RAM` sets the VMA region. `AT > FLASH` sets the LMA region. The linker emits the `.data` section's *bytes* into the `.elf` at addresses in `FLASH`; the *symbol values* (i.e. `&boot_counter`) point into `RAM`.

The startup file needs three symbols to do the copy:

```
_sidata = LOADADDR(.data);   /* "start of initialized data" — the LMA */

.data : {
    _sdata = .;              /* current . is the VMA, so this is VMA-start */
    *(.data*)
    _edata = .;              /* VMA-end */
} > RAM AT > FLASH
```

In the reset handler:

```c
extern uint32_t _sidata, _sdata, _edata;
const uint32_t *src = &_sidata;
uint32_t *dst = &_sdata;
while (dst < &_edata) *dst++ = *src++;
```

This loop copies bytes from `_sidata` (in flash) to `_sdata`–`_edata` (in SRAM). After it runs, `boot_counter` reads as `42` from `*(volatile uint32_t *)&_sdata`. We will write this loop in Lecture 3.

**A common mistake.** Beginners write `> FLASH > RAM`, swapping the two regions. The result: the LMA is in RAM (where it dies on power-cycle) and the VMA is in flash (where writes silently fail). The firmware appears to work for the first boot (because `boot_counter` happens to be `42` in flash already) but loses its updates on every reboot. The fix: swap to `> RAM AT > FLASH`.

---

## 5. `.bss` and the `NOLOAD` directive

A zero-initialized global:

```c
uint32_t event_count;     /* equivalent to: uint32_t event_count = 0; */
```

This global has only a VMA — no LMA — because its initial value is "all zeroes" and that does not need to be stored in flash. It is enough for the reset handler to *write* zeroes into the SRAM region at boot.

The linker-script syntax:

```
.bss (NOLOAD) : {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    _ebss = .;
} > RAM
```

`NOLOAD` tells the linker: "this section has no bytes in the `.elf` file." The output ELF reserves the address range `[_sbss, _ebss)` but does not store any data for it. The reset handler will zero the range at boot.

`*(COMMON)` is a historical input section for "tentative definitions" in C (a global declared without an initializer at file scope). Modern GCC with `-fno-common` (the default since GCC 10) emits these as `.bss` directly, but older codebases rely on `COMMON` and the script includes it for compatibility.

The startup code's `.bss` zero loop:

```c
extern uint32_t _sbss, _ebss;
uint32_t *p = &_sbss;
while (p < &_ebss) *p++ = 0;
```

If you forget this loop, your `event_count` global starts at *whatever was in SRAM after the chip came out of reset* — which is undefined, sometimes 0, sometimes 0xAA pattern (RP2040 ROM behavior), sometimes garbage. Programs that depend on `event_count == 0` at startup will work intermittently. This is the second most common Week 3 bug.

---

## 6. The location counter `.`

Inside a `SECTIONS` block, the symbol `.` is the **location counter**: the current address being emitted. The linker maintains this as it walks the input sections.

You can read it:

```
_etext = .;       /* Record the address right after .text */
```

You can write it (to insert padding):

```
. = ALIGN(4);            /* Round . up to a 4-byte boundary */
. = . + 64;              /* Insert 64 bytes of padding */
. = 0x10000400;          /* Jump . to a specific absolute address (rare and dangerous) */
```

You can use it in expressions:

```
_section_size = . - _section_start;
```

The location counter is reset at the start of each output section. Between output sections, it carries over: if `.text` ends at `0x1000_0512`, the next section's `.` starts at `0x1000_0512`. The `ALIGN(n)` directive rounds it up, padding with the section's fill pattern (default `0x00`).

**Idiom: section-boundary symbols.** Most embedded linker scripts export pairs of symbols around each output section:

```
.bss (NOLOAD) : {
    _sbss = .;       /* start of .bss */
    *(.bss*)
    _ebss = .;       /* end of .bss */
} > RAM
```

These symbols are accessible from C via `extern uint32_t _sbss, _ebss;` and their *addresses* (`&_sbss`, `&_ebss`) are what you actually want. The convention is `_sX` / `_eX` (start/end), `__X_start__` / `__X_end__` (newlib), or `_X_start` / `_X_end` (Zephyr). Pick one set and stick with it.

---

## 7. `KEEP()` and `--gc-sections`

When you compile with `-ffunction-sections -fdata-sections`, GCC emits every function and every global in its own ELF input section: `.text.foo`, `.text.bar`, `.data.event_count`, and so on. When you link with `-Wl,--gc-sections`, the linker performs **garbage collection**: any input section *not referenced by another input section starting from the entry point* is removed.

This is a great size optimization for normal code — typically 30–60% binary-size reduction. But it has a footgun for two specific sections:

- **The vector table.** Nothing in C-land references the vector table by name. The boot ROM jumps to it via the magic address `[0x0000_0004]` (well, technically via `[__isr_vector + 4]` after `boot2`). The linker does not know about that. Without intervention, `--gc-sections` removes the vector table, and your firmware boots into garbage.
- **`boot2`.** Same problem: nothing references it from C. It is the very first thing the ROM jumps to. It must be kept.

The fix is the `KEEP()` directive in the linker script. Any input-section selector wrapped in `KEEP()` is exempt from garbage collection:

```
.boot2 : {
    KEEP(*(.boot2))
} > FLASH

.isr_vector : ALIGN(256) {
    KEEP(*(.isr_vector))
} > FLASH
```

The third common use of `KEEP()` is for `.init_array` and `.fini_array` — the C++ constructor arrays, used by `__libc_init_array`. We will not need them until Week 4 (C++ on bare metal), but the script declares them now to avoid having to revisit the script later.

A common debugging story: "my firmware does not boot, but it builds fine, and the `.elf` looks right." `arm-none-eabi-nm -n build/blink.elf | head` shows that `Reset_Handler` is at `0x1000_0100` — but `arm-none-eabi-objdump -h build/blink.elf` shows the `.isr_vector` section has size `0x00000000`. The fix is to wrap the input selector in `KEEP()`. The vector table was garbage-collected.

---

## 8. `PROVIDE`, `PROVIDE_HIDDEN`, and `ENTRY`

Three more directives you will see in real linker scripts.

**`PROVIDE(sym = expr)`** defines a symbol *only if it is not already defined* in any object file. The idiom for a default stack-top is:

```
PROVIDE(_estack = ORIGIN(RAM) + LENGTH(RAM));
```

If some object file defines `_estack` itself (say, an RTOS port that wants a smaller initial stack), that definition wins. Otherwise the linker script's default applies. `PROVIDE_HIDDEN` is the same but the resulting symbol has hidden visibility, so it does not show up in shared-library exports (irrelevant on a bare-metal MCU, but stylistic in some scripts).

**`ENTRY(symbol)`** sets the entry-point symbol in the ELF header. On bare-metal Cortex-M this is informational — the boot ROM uses the vector table, not the ELF entry — but it shows up in `arm-none-eabi-readelf -h` and in the `.map` file. Convention: `ENTRY(Reset_Handler)`.

**`OUTPUT_FORMAT("elf32-littlearm")`** sets the output binary format. The default for `arm-none-eabi-ld` is already `elf32-littlearm`, so this line is usually omitted. Some scripts include it for clarity.

---

## 9. Input-section wildcards: `*(.text*)`, `KEEP(*(.isr_vector))`, `SORT(.init_array.*)`

The input-section selectors inside an output-section body are wildcards:

- `*(.text)` — match input sections named exactly `.text` from any object file.
- `*(.text*)` — match `.text`, `.text.main`, `.text.gpio_init`, etc. The `*` is a `glob`, not a regex.
- `*(.text .text.*)` — explicit list. Equivalent to the above for most purposes.
- `KEEP(*(.text*))` — same match, but exempt from `--gc-sections`.
- `*(SORT(.init_array.*))` — match `.init_array.NNNNN` and sort by suffix. C++ constructors have priority suffixes (`__attribute__((init_priority(300)))`), and they must run in order.
- `*(.text.foo)` — match only the input section `.text.foo` from any object file.
- `libc.a:*(.text*)` — match `.text*` only from the archive file `libc.a`. Useful for routing libc's `.text` into a different region.

The `*` before the parentheses is the **filename wildcard** (which object file). The `(.text*)` is the **section wildcard** (which input section name). Most embedded scripts use `*(...)` (any file) for simplicity.

---

## 10. The `.map` file — the linker's audit log

When you pass `-Map=blink.map` to `ld`, the linker emits a text file describing every decision it made: which input section landed in which output section, at what VMA, with what LMA, with what size. The map file is the second-best debugging tool for linker problems (the best is `arm-none-eabi-objdump -h build/blink.elf`).

A snippet from a real `blink.map`:

```
Memory Configuration

Name             Origin             Length             Attributes
FLASH            0x0000000010000000 0x0000000000200000 xr
RAM              0x0000000020000000 0x0000000000040000 xrw
*default*        0x0000000000000000 0xffffffffffffffff

Linker script and memory map

                0x0000000010000000                . = 0x10000000
                0x0000000010000000                _stext = .

.boot2          0x0000000010000000       0x100
 *(.boot2)
 .boot2         0x0000000010000000      0xfc boot2.o
                0x00000000100000fc                . = ALIGN (0x100)

.isr_vector     0x0000000010000100       0xc0
                0x0000000010000100                __isr_vector = .
 *(.isr_vector)
 .isr_vector    0x0000000010000100      0xc0 startup.o

.text           0x00000000100001c0     0x1d4c
 *(.text*)
 .text.main     0x00000000100001c0      0x68 main.o
 .text.gp15_init 0x0000000010000228     0x4c main.o
 …
```

Read this top to bottom and you have a complete inventory of what is in flash, in what order, with what size. The most common things to check:

- **Is `.isr_vector` at `0x1000_0100`?** If not, `boot2` is missing or wrong-sized.
- **Is `Reset_Handler` an address inside `0x1000_0100`–`0x1000_0140`?** It should be one of the early vector-table entries.
- **Are `_sdata` and `_edata` in RAM?** They should be `0x2000_xxxx`. The `_sidata` symbol should be in FLASH (`0x1000_xxxx`), right after `.text`.
- **Is `.bss` size as expected?** Compare with what `arm-none-eabi-size build/blink.elf` reports. If `.bss` is much bigger than expected, look for a stray large array.

Open the map file in any text editor. The first thing you should do after every build of a new firmware is `less build/blink.map | grep -A 2 'isr_vector\|.data\|.bss'`.

---

## 11. Building and verifying the script

To use the script in Exercise 3:

```bash
arm-none-eabi-gcc -c \
    -mcpu=cortex-m0plus -mthumb \
    -ffreestanding -fno-builtin -nostdlib \
    -O2 -g \
    -ffunction-sections -fdata-sections \
    startup.c main.c -o startup.o main.o

arm-none-eabi-gcc -c -x assembler-with-cpp \
    -mcpu=cortex-m0plus -mthumb \
    boot2_w25q080.S -o boot2.o

arm-none-eabi-ld \
    -T pico.ld \
    -Map=build/blink.map \
    --gc-sections \
    -o build/blink.elf \
    startup.o main.o boot2.o
```

The `--gc-sections` flag activates garbage collection. The `-Map=` flag dumps the map file. Skip neither.

Verify with:

```bash
arm-none-eabi-objdump -h build/blink.elf
arm-none-eabi-nm -n -S build/blink.elf | head -20
arm-none-eabi-size build/blink.elf
```

Expected output of `objdump -h` (excerpt):

```
Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .boot2        00000100  10000000  10000000  00010000  2**0
  1 .isr_vector   000000c0  10000100  10000100  00010100  2**8
  2 .text         00001d4c  100001c0  100001c0  000101c0  2**2
  3 .data         00000010  20000000  10001f0c  00030000  2**2
  4 .bss          00000040  20000010  20000010  00030010  2**2
```

The columns `VMA` and `LMA` for `.data` differ — VMA in RAM (`0x2000_0000`), LMA in flash (`0x1000_1f0c`). That is the line you check first.

---

## 12. Common mistakes and their symptoms

| Mistake | Symptom |
|---------|---------|
| Forgot `KEEP(*(.isr_vector))` | Firmware boots into garbage after `boot2`. `objdump -h` shows `.isr_vector` size = 0. |
| Wrote `> FLASH > RAM` instead of `> RAM AT > FLASH` | Globals lose their initial values on every reboot. |
| Forgot `--gc-sections` in linker invocation | Binary is ~3x larger than expected; unused functions still in flash. |
| Forgot `_estack = ORIGIN(RAM) + LENGTH(RAM);` | Vector table word 0 is undefined; on reset, SP is garbage; first push HardFaults. |
| `.bss` size much larger than expected | A stray large array — usually a buffer in a header that gets included in every translation unit. |
| `_sidata` and `_sdata` are equal | LMA = VMA — the linker placed `.data` only in RAM, no flash copy. The first reboot works because RAM happens to hold the value; later boots fail. Usually a missing `AT > FLASH`. |
| Output `.elf` is huge and `objdump` shows debug sections taking 60% | Forgot to `arm-none-eabi-strip` or use `objcopy -O binary` for the final image. |
| `.data` is empty in `.map` but you have initialized globals | You forgot `-fdata-sections`; the linker cannot see individual globals, only the whole `.data` from each `.o`. |

---

## 13. What you should be able to do after this lecture

- Read the script in §3 line by line and explain what every directive does.
- Author a minimal `pico.ld` for the RP2040, ~60 lines, that places vectors at `0x1000_0100`, code in flash, data with VMA/LMA split, and `.bss` zeroed in RAM.
- Compute, given a global `uint8_t buf[1024] = {1,2,3,…};`, what the size of `.text`, `.data`, and `.bss` will each be. (Answer: `buf` is initialized, so `.data` grows by 1024 bytes and `.text` grows by the LMA copy.)
- Predict, given a global `static uint8_t scratch[4096];` with no initializer, what the size of `.text`, `.data`, and `.bss` will each be. (Answer: `scratch` is uninitialized, so `.bss` grows by 4096 bytes and `.text` and `.data` are unchanged.)
- Explain `KEEP()` to a teammate in one sentence: "Without it, `--gc-sections` removes input sections that nothing in C-land references — including the vector table, which is reached by the hardware via an absolute address, not a call."

---

## 14. Reading list for tomorrow

Before Lecture 3, read:

- GNU `ld` documentation §3.5 (Linker Script Expressions) — the full expression language.
- GNU `ld` documentation §3.6.8.4 (Output Section Keywords) — `KEEP`, `PROVIDE`, `BYTE`/`SHORT`/`LONG`.
- ARM Cortex-M0+ Generic User Guide §2.3 (Exception model) — vectors, exception entry, the way the processor finds the vector table.

Bring questions to Wednesday studio.

---

Next up: writing the startup file that consumes the symbols this script exports — and the vector table whose layout the architecture dictates.
