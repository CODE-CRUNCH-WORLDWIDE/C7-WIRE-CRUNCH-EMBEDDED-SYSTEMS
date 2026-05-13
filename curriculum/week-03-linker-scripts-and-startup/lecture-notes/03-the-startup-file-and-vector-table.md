# Lecture 3 — The Startup File and the Vector Table

> **Outcome:** You can write a Cortex-M0+ startup file in C, ~120 lines, that includes the vector table for the RP2040, a reset handler that copies `.data` and zeroes `.bss`, default handlers for every exception, and a sensible HardFault handler that traps the CPU in an infinite loop. You can explain, in one sentence each, what the boot ROM does, what `boot2` does, what `Reset_Handler` does, and what runs first inside `main()`.

---

## 1. The job of the startup file

The startup file is the bridge between hardware reset and your `main()`. Its responsibilities, in strict order:

1. **Define the vector table.** A 48-entry array of function pointers placed at the start of flash (right after `boot2` on the RP2040). The hardware reads word 0 of this table to get the initial stack pointer, and word 1 to get the reset handler's address.
2. **Define the reset handler.** The first C function the CPU executes. Its job is to copy `.data` from flash to SRAM, zero `.bss`, run static constructors (Week 4 only — we will skip this week), and call `main()`.
3. **Define default handlers for every other exception.** NMI, HardFault, SVCall, PendSV, SysTick, and the 26 external IRQs. The default handler is usually a `while (1) {}` — a deliberate trap to catch unhandled interrupts. Real handlers override the default via weak symbols.
4. **Define `_estack`** if the linker script does not. Most scripts do; we will assume so.

That is all. ~120 lines of C. By the end of this lecture you will have written every line.

---

## 2. The vector table — what the hardware demands

From the ARM Cortex-M0+ Generic User Guide §2.3.4 (Vector table), p. 2-17:

> On system reset, the vector table is fixed at address 0x00000000. Privileged software can write to the VTOR to relocate the vector table start address to a different memory location, in the range 0x00000080 to 0x3FFFFF80 (see Vector Table Offset Register on page 4-19).

And from the same section:

> The vector table contains the reset value of the stack pointer, and the start addresses, also known as exception vectors, for all exception handlers.

The table layout, for ARMv6-M / Cortex-M0+:

```
   Offset   Index   Vector            Description
   0x000    0       Initial SP        Loaded into MSP on reset
   0x004    1       Reset             Reset handler
   0x008    2       NMI               Non-maskable interrupt
   0x00C    3       HardFault         All faults (M0+ has no fault distinction)
   0x010    4–10    Reserved          (5 architecturally reserved + 2 reserved-for-MPU)
   0x02C    11      SVCall            SVC instruction
   0x030    12–13   Reserved
   0x038    14      PendSV            Software-pended interrupt, used by RTOSes
   0x03C    15      SysTick           24-bit countdown timer
   0x040    16      External IRQ 0    RP2040 IRQ 0 (TIMER_IRQ_0)
   0x044    17      External IRQ 1    RP2040 IRQ 1 (TIMER_IRQ_1)
   …
   0x0BC    47      External IRQ 31   (RP2040 only has 26; slots 26-31 reserved)
```

The first 16 vectors are **architecturally defined** by ARM and are identical on every Cortex-M0+ part. The remaining 32 slots are **vendor-defined**: the RP2040 uses slots 16–41 for its 26 IRQs, listed in datasheet §2.3.2, p. 56.

From RP2040 datasheet §2.3.2, p. 56 (Sep-2024 rev):

```
   IRQ #   Source
   0       TIMER_IRQ_0
   1       TIMER_IRQ_1
   2       TIMER_IRQ_2
   3       TIMER_IRQ_3
   4       PWM_IRQ_WRAP
   5       USBCTRL_IRQ
   6       XIP_IRQ
   7       PIO0_IRQ_0
   8       PIO0_IRQ_1
   9       PIO1_IRQ_0
   10      PIO1_IRQ_1
   11      DMA_IRQ_0
   12      DMA_IRQ_1
   13      IO_IRQ_BANK0
   14      IO_IRQ_QSPI
   15      SIO_IRQ_PROC0
   16      SIO_IRQ_PROC1
   17      CLOCKS_IRQ
   18      SPI0_IRQ
   19      SPI1_IRQ
   20      UART0_IRQ
   21      UART1_IRQ
   22      ADC_IRQ_FIFO
   23      I2C0_IRQ
   24      I2C1_IRQ
   25      RTC_IRQ
```

26 IRQs, numbered 0–25. The vector table reserves slots 16–47 (32 slots total) for external IRQs; the RP2040 uses 16–41 and leaves 42–47 reserved.

Word 0 of the table is **not a function pointer**. It is the initial value loaded into the Main Stack Pointer (MSP) at reset. The hardware reads `[__isr_vector + 0]` into `SP` and `[__isr_vector + 4]` into `PC` before fetching a single instruction. This is in DUI 0662B §2.3.4, p. 2-17.

This means your `_estack` symbol — the top of SRAM — must be the value stored at word 0 of the vector table. The linker script defines `_estack = ORIGIN(RAM) + LENGTH(RAM) = 0x2004_0000`, and the startup file's vector table starts with `(uint32_t)&_estack`.

---

## 3. Writing the vector table in C

Here is the vector table as a C array. Place it in a section called `.isr_vector` so the linker can put it at the start of flash (after `boot2`):

```c
/* startup.c — C7 · Crunch Wire — Week 03
 *
 * Cortex-M0+ vector table + reset handler for RP2040.
 * Citations: DUI 0662B §2.3 (exception model); RP2040 datasheet §2.3.2 p.56.
 */

#include <stdint.h>

extern uint32_t _estack;       /* from pico.ld */
extern uint32_t _sidata;
extern uint32_t _sdata, _edata;
extern uint32_t _sbss, _ebss;

extern int main(void);

/* Forward declarations. Each handler is __attribute__((weak, alias))
 * so the user can override any of them by defining a real function
 * with the same name in another .c file. */

void Default_Handler(void);    /* the catch-all infinite loop */
void Reset_Handler(void);      /* the one we always define */

#define DEFAULT_HANDLER(name) \
    void name(void) __attribute__((weak, alias("Default_Handler")))

DEFAULT_HANDLER(NMI_Handler);
DEFAULT_HANDLER(HardFault_Handler);
DEFAULT_HANDLER(SVCall_Handler);
DEFAULT_HANDLER(PendSV_Handler);
DEFAULT_HANDLER(SysTick_Handler);

/* RP2040 external IRQs, slots 16-41 of the vector table. */
DEFAULT_HANDLER(TIMER_IRQ_0_Handler);
DEFAULT_HANDLER(TIMER_IRQ_1_Handler);
DEFAULT_HANDLER(TIMER_IRQ_2_Handler);
DEFAULT_HANDLER(TIMER_IRQ_3_Handler);
DEFAULT_HANDLER(PWM_IRQ_WRAP_Handler);
DEFAULT_HANDLER(USBCTRL_IRQ_Handler);
DEFAULT_HANDLER(XIP_IRQ_Handler);
DEFAULT_HANDLER(PIO0_IRQ_0_Handler);
DEFAULT_HANDLER(PIO0_IRQ_1_Handler);
DEFAULT_HANDLER(PIO1_IRQ_0_Handler);
DEFAULT_HANDLER(PIO1_IRQ_1_Handler);
DEFAULT_HANDLER(DMA_IRQ_0_Handler);
DEFAULT_HANDLER(DMA_IRQ_1_Handler);
DEFAULT_HANDLER(IO_IRQ_BANK0_Handler);
DEFAULT_HANDLER(IO_IRQ_QSPI_Handler);
DEFAULT_HANDLER(SIO_IRQ_PROC0_Handler);
DEFAULT_HANDLER(SIO_IRQ_PROC1_Handler);
DEFAULT_HANDLER(CLOCKS_IRQ_Handler);
DEFAULT_HANDLER(SPI0_IRQ_Handler);
DEFAULT_HANDLER(SPI1_IRQ_Handler);
DEFAULT_HANDLER(UART0_IRQ_Handler);
DEFAULT_HANDLER(UART1_IRQ_Handler);
DEFAULT_HANDLER(ADC_IRQ_FIFO_Handler);
DEFAULT_HANDLER(I2C0_IRQ_Handler);
DEFAULT_HANDLER(I2C1_IRQ_Handler);
DEFAULT_HANDLER(RTC_IRQ_Handler);

/* The vector table itself. The __attribute__((section(".isr_vector")))
 * places this in the .isr_vector output section, which the linker script
 * places at flash offset 0x100 (immediately after the 256-byte boot2). */

__attribute__((section(".isr_vector"), used))
const void * const __isr_vector[48] = {
    /*  0 */ (void *)&_estack,
    /*  1 */ Reset_Handler,
    /*  2 */ NMI_Handler,
    /*  3 */ HardFault_Handler,
    /*  4 */ 0,
    /*  5 */ 0,
    /*  6 */ 0,
    /*  7 */ 0,
    /*  8 */ 0,
    /*  9 */ 0,
    /* 10 */ 0,
    /* 11 */ SVCall_Handler,
    /* 12 */ 0,
    /* 13 */ 0,
    /* 14 */ PendSV_Handler,
    /* 15 */ SysTick_Handler,
    /* RP2040 IRQs 0..25 (slots 16..41) */
    /* 16 */ TIMER_IRQ_0_Handler,
    /* 17 */ TIMER_IRQ_1_Handler,
    /* 18 */ TIMER_IRQ_2_Handler,
    /* 19 */ TIMER_IRQ_3_Handler,
    /* 20 */ PWM_IRQ_WRAP_Handler,
    /* 21 */ USBCTRL_IRQ_Handler,
    /* 22 */ XIP_IRQ_Handler,
    /* 23 */ PIO0_IRQ_0_Handler,
    /* 24 */ PIO0_IRQ_1_Handler,
    /* 25 */ PIO1_IRQ_0_Handler,
    /* 26 */ PIO1_IRQ_1_Handler,
    /* 27 */ DMA_IRQ_0_Handler,
    /* 28 */ DMA_IRQ_1_Handler,
    /* 29 */ IO_IRQ_BANK0_Handler,
    /* 30 */ IO_IRQ_QSPI_Handler,
    /* 31 */ SIO_IRQ_PROC0_Handler,
    /* 32 */ SIO_IRQ_PROC1_Handler,
    /* 33 */ CLOCKS_IRQ_Handler,
    /* 34 */ SPI0_IRQ_Handler,
    /* 35 */ SPI1_IRQ_Handler,
    /* 36 */ UART0_IRQ_Handler,
    /* 37 */ UART1_IRQ_Handler,
    /* 38 */ ADC_IRQ_FIFO_Handler,
    /* 39 */ I2C0_IRQ_Handler,
    /* 40 */ I2C1_IRQ_Handler,
    /* 41 */ RTC_IRQ_Handler,
    /* 42-47: reserved, zeroed */
    /* 42 */ 0, /* 43 */ 0, /* 44 */ 0,
    /* 45 */ 0, /* 46 */ 0, /* 47 */ 0,
};
```

Three observations:

- **`__attribute__((used))`** prevents GCC from removing the array as "defined-but-unused at the C level." Combined with `KEEP(*(.isr_vector))` in the linker script, the table survives both compiler dead-code elimination and linker garbage collection.
- **The reserved slots are stored as `0`, not as function pointers.** A `0` in a function-pointer slot is a deliberate poison: if the hardware ever vectors there (which it should not, since the slot is reserved), the resulting `BLX r0` would jump to `0x00000000` and execute the boot ROM, which is a well-defined (if useless) state. Some startup files store `0xFFFFFFFE` instead — same idea, different poison value.
- **`Reset_Handler` is not declared weak.** We always provide it ourselves. The other handlers (`NMI_Handler`, `HardFault_Handler`, etc.) are weak aliases to `Default_Handler`, so a user-provided real handler overrides them at link time. This is the standard CMSIS startup-file pattern.

---

## 4. The reset handler — what runs before `main()`

Here is `Reset_Handler`. Approximately 25 lines of C:

```c
__attribute__((noreturn))
void Reset_Handler(void) {

    /* Step 1. The boot ROM has already loaded SP from __isr_vector[0]
     * before calling boot2. boot2 returns into us with SP intact.
     * (If we were to take a soft reset via SCB.AIRCR.SYSRESETREQ later,
     * the hardware would reload SP from __isr_vector[0] again.) */

    /* Step 2. Copy .data from flash to SRAM.
     * _sidata is the LMA (in flash); _sdata..._edata is the VMA range. */
    const uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* Step 3. Zero .bss. */
    uint32_t *bss = &_sbss;
    while (bss < &_ebss) {
        *bss++ = 0;
    }

    /* Step 4. Call C++ static constructors, if any.
     * This week (Week 3, plain C) the array is empty and this is a no-op.
     * Week 4 will fill it.
     *
     *   extern void (*__preinit_array_start[])(void);
     *   extern void (*__preinit_array_end[])(void);
     *   for (size_t i = 0; &__preinit_array_start[i] < __preinit_array_end; i++)
     *       __preinit_array_start[i]();
     *   ...same for __init_array...
     */

    /* Step 5. Call main(). */
    (void)main();

    /* Step 6. If main() returns, loop forever.
     * Returning from main on a bare-metal MCU is almost always a bug;
     * we trap here so the debugger can see we got here. */
    while (1) {
        __asm__ volatile ("wfe");
    }
}
```

Notice three things:

- **The whole thing is straight-line C with no assembly.** On Cortex-M0+ you do not need to write the startup in assembly; the architecture sets `SP` for you from `__isr_vector[0]` before your code runs, so the `Reset_Handler` already has a valid stack. Older Cortex-M reference designs (e.g. the CMSIS template `startup_ARMCM0plus.S`) write the startup in assembly, but C works equally well and is more readable.
- **The `.data` copy and `.bss` zero loops use the linker-script symbols directly via `extern`.** This is why Lecture 2 emphasized exporting `_sidata`, `_sdata`, `_edata`, `_sbss`, `_ebss` from the script. The C code does not know or care about absolute addresses; it just walks the regions the linker carved out.
- **`__attribute__((noreturn))`** tells GCC the function never returns. It enables tail-call optimization and lets GCC omit the function epilogue. Without it, the compiler might emit a `pop {pc}` after the `while(1)`, which would crash (the stack is the initial stack, no return address pushed) if it ever ran.

---

## 5. The default handler

The `Default_Handler` is what every unhandled exception or IRQ falls into:

```c
__attribute__((noreturn))
void Default_Handler(void) {
    /* An unhandled exception or IRQ. Trap forever; a debugger can stop here. */
    while (1) {
        __asm__ volatile ("bkpt #0");   /* halt the CPU; debugger sees this */
    }
}
```

The `bkpt #0` instruction halts the CPU and signals a debugger break. If no debugger is attached, the instruction acts as a HardFault on M0+ (Cortex-M0+ does not have a separate breakpoint behavior in non-debug mode), which re-enters `HardFault_Handler` — also `Default_Handler` by default — and so on. The net effect: the CPU is stuck. A `printf("unhandled IRQ\n");` here is tempting, but a UART driver might not be initialized when an early IRQ fires, so the trap is the safer default.

For a real product you would replace `Default_Handler` with one that grabs the current `IPSR` (Interrupt Program Status Register, holds the active exception number), writes it to a known SRAM location, and then resets. The next boot dumps the saved IPSR over UART. We will do this in Week 7 (Interrupts) — for Week 3 the infinite loop is correct.

---

## 6. The HardFault handler — first-line forensics

The default `HardFault_Handler` is also an infinite loop. That is acceptable for Week 3 but anemic in a real product. A modestly useful HardFault handler looks like this:

```c
/* In a real product, replace the weak alias with a custom handler.
 * Grab the stacked registers so a debugger sees where the fault came from. */

__attribute__((naked, noreturn))
void HardFault_Handler_real(void) {
    __asm__ volatile (
        "mrs   r0, msp        \n"   /* r0 = current MSP */
        "ldr   r1, [r0, #24]  \n"   /* r1 = stacked PC */
        "ldr   r2, [r0, #20]  \n"   /* r2 = stacked LR */
        "b     .              \n"   /* spin; debugger sees r0/r1/r2 */
    );
}
```

The `naked` attribute tells GCC to emit no prologue or epilogue. The handler is pure assembly. After the fault, the processor has pushed `R0-R3, R12, LR, PC, xPSR` onto the stack in that order; we read the stacked PC (offset 24) to learn the instruction that faulted, and the stacked LR (offset 20) to learn its caller. With a debugger attached, we can read these from `r1` and `r2`. Without one, we are stuck — but at least the debugger story works.

Week 3 you do not need this. Week 7 you will write a richer version. Mention it in your `Default_Handler` comment so future-you remembers.

---

## 7. The `boot2` problem — a 256-byte side quest

The boot ROM expects `boot2` at the very first byte of flash, `0x1000_0000`. `boot2` is responsible for configuring the W25Q080 QSPI flash chip for continuous-read mode so that the rest of your firmware can run from flash addresses `0x1000_0100` onward via XIP.

You **do not** write `boot2` from scratch. You borrow the official copy from `pico-sdk/src/rp2_common/boot_stage2/boot2_w25q080.S`. It is a ~140-line ARM assembly file plus a Python `pad_checksum` script that appends the 4-byte CRC. The linker script places it in the `.boot2` output section, padded to 256 bytes.

The minimal way to incorporate `boot2` without bringing in the rest of the SDK:

1. Copy `boot2_w25q080.S` from `pico-sdk` into your project.
2. Copy `pad_checksum` (Python script, ~50 lines) from `pico-sdk/src/rp2_common/boot_stage2/`.
3. In your `Makefile`, build `boot2.elf` from `boot2_w25q080.S`, run `pad_checksum` to produce a 256-byte `boot2.bin`, then convert `boot2.bin` back to an object file (`arm-none-eabi-objcopy -I binary -O elf32-littlearm -B armv6-m --rename-section .data=.boot2 boot2.bin boot2.o`).
4. Link `boot2.o` first.

This is the only file we borrow from the SDK. It is hardware-specific (W25Q080 instruction set) and rewriting it would require a deep dive into QSPI flash chip programming, which is a Week 11 topic.

There is a more aggressive option — Challenge 1 this week — which skips `boot2` entirely by loading the firmware into SRAM over SWD with OpenOCD or probe-rs. No flash, no `boot2`, no XIP setup. The firmware is volatile (gone on power-cycle), but the build is dramatically simpler and the boot chain has fewer moving parts. We will explore that in Friday's challenge.

---

## 8. Putting it all together

The full `startup.c` is in §3 above plus the `Reset_Handler` in §4. The full size is ~150 lines of C plus ~50 lines of comments and `extern` declarations. Combined with the linker script from Lecture 2, that is everything you need to boot the RP2040 to `main()`.

The build sequence:

```bash
arm-none-eabi-gcc -c \
    -mcpu=cortex-m0plus -mthumb \
    -ffreestanding -fno-builtin -nostdlib \
    -O2 -g \
    -ffunction-sections -fdata-sections \
    startup.c main.c -o build/startup.o build/main.o

arm-none-eabi-as \
    -mcpu=cortex-m0plus -mthumb \
    boot2_w25q080.S -o build/boot2_raw.o

# Append the 4-byte CRC checksum to boot2
python3 pad_checksum -s 0xffffffff build/boot2_raw.bin build/boot2.bin
arm-none-eabi-objcopy -I binary -O elf32-littlearm -B armv6-m \
    --rename-section .data=.boot2 \
    build/boot2.bin build/boot2.o

arm-none-eabi-ld \
    -T pico.ld \
    -Map=build/blink.map \
    --gc-sections \
    -o build/blink.elf \
    build/boot2.o build/startup.o build/main.o

arm-none-eabi-objcopy -O binary build/blink.elf build/blink.bin
elf2uf2 build/blink.elf build/blink.uf2
```

Drag `blink.uf2` to a Pi Pico W in BOOTSEL mode, and the firmware boots. By Sunday this build will take you ~30 seconds end-to-end.

---

## 9. What runs first inside `main()`?

Once `Reset_Handler` calls `main()`, you are in C-with-no-runtime-services. The libc is not initialized. There is no `malloc`. There is no `printf` unless you link `libc.a` (a separate decision). There is no thread, no scheduler, no signal handling. Just your code, the registers, and the stack.

The first three lines of `main()` for a typical Week 3 firmware:

```c
int main(void) {
    /* 1. Bring the peripherals we want out of reset. */
    *(volatile uint32_t *)0x4000c000u = 0u;        /* RESETS_RESET = 0: nothing held in reset */
    /* (Real firmware uses the SET/CLR aliases and only un-resets the
     * specific peripherals it needs; we are sloppy here for brevity.) */

    /* 2. Wait for the un-reset to take effect by polling RESETS_DONE. */
    while ((*(volatile uint32_t *)0x4000c008u & 0xffffffu) != 0xffffffu) { }

    /* 3. Run the application. */
    blink_loop();
    return 0;
}
```

The `RESETS_RESET` register is at `0x4000c000` (RP2040 datasheet §2.14.3, p. 219). Out of reset, *every peripheral is held in reset* (the register defaults to all-ones). Until you clear the corresponding bit, the peripheral does not respond to register reads or writes. The Pi Pico SDK's `pico_init` calls `unreset_block` for the peripherals it needs; we do it by hand.

This is the moment Week 3 ties back to Week 2: the GPIO writes you mastered in Week 2 will silently fail if you have not first un-reset the IO_BANK0 and PADS_BANK0 peripherals here. The SDK hides this; Week 3 makes you face it.

---

## 10. What you should be able to do after this lecture

- Write a Cortex-M0+ vector table for the RP2040 from a blank C file, including the 16 architectural vectors and the 26 RP2040 external IRQs.
- Write a `Reset_Handler` that copies `.data`, zeroes `.bss`, and calls `main()`.
- Define `Default_Handler` as an infinite-loop trap, and use `__attribute__((weak, alias))` to make every IRQ handler default to it.
- Explain why the vector table's first word is the initial `SP` value, not a function pointer.
- Explain why `boot2` is a separate file we borrow, not something we write from scratch.
- Explain why the first thing `main()` should do is un-reset the peripherals it intends to use.

---

## 11. Reading list for Thursday

Before Exercise 3, read:

- ARM Cortex-M0+ Generic User Guide §2.3 (Exception model), pp. 2-13 to 2-23. Read it twice.
- RP2040 datasheet §2.8 (Boot Sequence), pp. 130–145. Skim §2.8.1.1 in detail.
- The Pi Pico SDK file `src/rp2_common/pico_standard_link/crt0.S`. Compare it line-by-line with the C `startup.c` you are about to write. The SDK version is in assembly and ~200 lines; yours will be in C and ~150 lines. They do the same thing.

---

## 12. Common questions

**"Why is the initial SP at the top of RAM, not the top of `.bss`?"** Because the stack grows downward. The Cortex-M ABI requires SP-aligned-to-8-bytes on function entry, and the stack frame for a deep call chain may use multiple kilobytes. By starting at the top of SRAM and growing down, the stack uses whatever RAM is left over after `.data` and `.bss`. If your `.bss` is small and your `.data` is small, the stack has the entire SRAM minus those two regions to use.

**"What happens if the stack collides with `.bss`?"** Undefined behavior. The stack will overwrite `.bss`, your globals will start changing unpredictably, the program will appear to "haunt itself." The linker-script `ASSERT` in Lecture 2 reserves at least 4 KiB and refuses to link otherwise; this catches obvious overflows at build time, but a deep recursion at runtime still produces the bug.

**"Why is the vector table 48 entries and not, say, 32?"** The architecture mandates 16 (the first 16 for system exceptions), plus up to 240 external IRQ slots. RP2040 uses 26 IRQs. 16 + 32 = 48 leaves comfortable headroom; some startup files use exactly 16 + 26 = 42. The trade-off is alignment: 48 × 4 = 192 bytes, padded to 256 bytes per VTOR's alignment rule (next power of two ≥ 32 × 4 = 128, which is 128, but most scripts use 256 for safety). The 256-byte alignment is why `.boot2` is exactly 256 bytes — so that `.isr_vector` lands at `0x1000_0100`, neatly aligned.

**"Can I skip the `.data` copy if I have no initialized globals?"** Yes — but the loop is two cycles per word and runs at boot only, so the overhead is irrelevant. Keep the loop; it costs nothing and guards against the case where you add an initialized global later and forget.

**"Can I skip the `.bss` zero if my C code only uses initialized globals?"** No. Uninitialized `static` locals (with no explicit `= 0`) end up in `.bss`, and the C standard guarantees they start as zero. Skipping the zero loop violates the standard and produces bugs that surface only after a power-cycle.

**"What about `static` constructors for C globals?"** In C (not C++), there are none. Global initializers in C are compile-time constants; they end up in `.data` (if non-zero) or `.bss` (if zero). C++ allows runtime constructors for globals (`MyClass g_thing;`), and those constructors run from `__libc_init_array`, which the reset handler calls. Week 3 is plain C, so we skip the `__libc_init_array` call (and the array is empty anyway).

**"What if I want to relocate the vector table to SRAM at runtime?"** Write the new address to `VTOR` at `0xe000_ed08`. This is useful for a bootloader: the bootloader has its own vector table in flash at `0x1000_0100`; the application has its own vector table at `0x2002_0000` (or wherever in SRAM). The bootloader, before jumping to the application, sets `VTOR = 0x2002_0000` so subsequent interrupts vector to the application's handlers. This is a Week 9 (Bootloaders) topic.

---

Next: bring it to the bench. Exercises 1–3 walk you through reading a real linker script, writing your own startup, and producing a flashable `.uf2` with no SDK code.
