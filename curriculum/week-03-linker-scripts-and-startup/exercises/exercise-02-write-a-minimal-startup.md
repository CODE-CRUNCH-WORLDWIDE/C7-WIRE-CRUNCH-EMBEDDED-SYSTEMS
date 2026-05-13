# Exercise 2 — Write a Minimal Startup File

**Time estimate:** ~120 minutes (~30 of coding, ~90 of debugging the first run).

## Problem statement

Write `startup.c` for a Pi Pico W from a blank file: the vector table, the reset handler, and the default exception handler. Build it alongside a single-purpose `main.c` that proves the runtime works — specifically, prove that `.bss` is zeroed before `main()` runs, and prove that an initialized global has its initial value after the `.data` copy.

For this exercise you will use the **SDK's** linker script (`memmap_default.ld`) and the SDK's `boot2` — you replace only the SDK's `crt0.S`. The next exercise replaces everything else. We split the work so you can debug one piece at a time.

## Acceptance criteria

- [ ] A new directory `c7-week03-startup/` containing `startup.c`, `main.c`, `CMakeLists.txt`, and `pico_sdk_import.cmake`.
- [ ] `startup.c` is **your code, typed**, not pasted from Lecture 3. The vector table, the reset handler, the default handler are all yours.
- [ ] `main.c` declares one initialized global (e.g. `uint32_t boot_counter = 0x12345678;`) and one uninitialized global (e.g. `uint32_t event_counter;`). Inside `main()`, the firmware writes the values of both to a known SRAM location *and* to a UART output, then enters a 1-Hz blink loop.
- [ ] On boot, observed via UART0 at 115200 8N1 on a Saleae or terminal:
  - `boot_counter` reads `0x12345678` (the `.data` copy ran).
  - `event_counter` reads `0x00000000` (the `.bss` zero ran).
- [ ] GP15 toggles at 1.0 Hz ± 1%, observed for at least 10 seconds.
- [ ] `arm-none-eabi-nm -n -S build/startup.elf | grep -E '__isr_vector|Reset_Handler|_estack'` resolves all three symbols to addresses inside `0x1000_0000`–`0x1010_0000` (vector table and reset handler) and `0x2004_0000` (stack top) respectively.
- [ ] A `notes/startup-walkthrough.md` documents:
  - What you typed into `startup.c`, with annotation per function.
  - The output of `arm-none-eabi-objdump -h build/startup.elf | head -15` showing the section layout.
  - The output of `arm-none-eabi-nm -n -S build/startup.elf | head -20` showing the first 20 symbols by address.
  - A Saleae or terminal screenshot showing the UART boot message with both globals' values.
- [ ] The `CMakeLists.txt` passes `-nostartfiles` to the linker so the SDK's `crt0.S` is excluded, and adds your `startup.c` as the replacement.

## Hints

<details>
<summary>The C source (`startup.c`)</summary>

```c
/* C7 · Crunch Wire — Week 03 — startup.c (your version)
 *
 * Minimal Cortex-M0+ startup for RP2040 / Pi Pico W. Targets the SDK's
 * memmap_default.ld linker script — replaces the SDK's crt0.S only.
 *
 * Citations: DUI 0662B §2.3 (exception model);
 *            RP2040 datasheet §2.3.2 p.56 (IRQ list);
 *            pico-sdk memmap_default.ld for the symbol names below.
 */

#include <stdint.h>

/* Symbols defined by the SDK's memmap_default.ld */
extern uint32_t __StackTop;       /* top of RAM, initial SP */
extern uint32_t __etext;          /* end of .text (= LMA of .data) */
extern uint32_t __data_start__;   /* VMA start of .data */
extern uint32_t __data_end__;     /* VMA end of .data */
extern uint32_t __bss_start__;
extern uint32_t __bss_end__;

extern int main(void);

void Default_Handler(void);
void Reset_Handler(void);

#define DEFAULT(name) \
    void name(void) __attribute__((weak, alias("Default_Handler")))

DEFAULT(NMI_Handler);
DEFAULT(HardFault_Handler);
DEFAULT(SVCall_Handler);
DEFAULT(PendSV_Handler);
DEFAULT(SysTick_Handler);
DEFAULT(TIMER_IRQ_0_Handler);
DEFAULT(TIMER_IRQ_1_Handler);
DEFAULT(TIMER_IRQ_2_Handler);
DEFAULT(TIMER_IRQ_3_Handler);
DEFAULT(PWM_IRQ_WRAP_Handler);
DEFAULT(USBCTRL_IRQ_Handler);
DEFAULT(XIP_IRQ_Handler);
DEFAULT(PIO0_IRQ_0_Handler);
DEFAULT(PIO0_IRQ_1_Handler);
DEFAULT(PIO1_IRQ_0_Handler);
DEFAULT(PIO1_IRQ_1_Handler);
DEFAULT(DMA_IRQ_0_Handler);
DEFAULT(DMA_IRQ_1_Handler);
DEFAULT(IO_IRQ_BANK0_Handler);
DEFAULT(IO_IRQ_QSPI_Handler);
DEFAULT(SIO_IRQ_PROC0_Handler);
DEFAULT(SIO_IRQ_PROC1_Handler);
DEFAULT(CLOCKS_IRQ_Handler);
DEFAULT(SPI0_IRQ_Handler);
DEFAULT(SPI1_IRQ_Handler);
DEFAULT(UART0_IRQ_Handler);
DEFAULT(UART1_IRQ_Handler);
DEFAULT(ADC_IRQ_FIFO_Handler);
DEFAULT(I2C0_IRQ_Handler);
DEFAULT(I2C1_IRQ_Handler);
DEFAULT(RTC_IRQ_Handler);

__attribute__((section(".vectors"), used))
const void * const __isr_vector[48] = {
    [0]  = (void *)&__StackTop,
    [1]  = Reset_Handler,
    [2]  = NMI_Handler,
    [3]  = HardFault_Handler,
    [11] = SVCall_Handler,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    [16] = TIMER_IRQ_0_Handler,
    [17] = TIMER_IRQ_1_Handler,
    [18] = TIMER_IRQ_2_Handler,
    [19] = TIMER_IRQ_3_Handler,
    [20] = PWM_IRQ_WRAP_Handler,
    [21] = USBCTRL_IRQ_Handler,
    [22] = XIP_IRQ_Handler,
    [23] = PIO0_IRQ_0_Handler,
    [24] = PIO0_IRQ_1_Handler,
    [25] = PIO1_IRQ_0_Handler,
    [26] = PIO1_IRQ_1_Handler,
    [27] = DMA_IRQ_0_Handler,
    [28] = DMA_IRQ_1_Handler,
    [29] = IO_IRQ_BANK0_Handler,
    [30] = IO_IRQ_QSPI_Handler,
    [31] = SIO_IRQ_PROC0_Handler,
    [32] = SIO_IRQ_PROC1_Handler,
    [33] = CLOCKS_IRQ_Handler,
    [34] = SPI0_IRQ_Handler,
    [35] = SPI1_IRQ_Handler,
    [36] = UART0_IRQ_Handler,
    [37] = UART1_IRQ_Handler,
    [38] = ADC_IRQ_FIFO_Handler,
    [39] = I2C0_IRQ_Handler,
    [40] = I2C1_IRQ_Handler,
    [41] = RTC_IRQ_Handler,
    /* slots 4-10, 12-13, 42-47 default to 0 */
};

__attribute__((noreturn))
void Reset_Handler(void) {
    /* Copy .data from flash LMA to SRAM VMA. */
    const uint32_t *src = &__etext;
    uint32_t *dst = &__data_start__;
    while (dst < &__data_end__) {
        *dst++ = *src++;
    }
    /* Zero .bss. */
    uint32_t *bss = &__bss_start__;
    while (bss < &__bss_end__) {
        *bss++ = 0;
    }
    /* Call main. */
    (void)main();
    /* If main returns, trap. */
    while (1) {
        __asm__ volatile ("wfe");
    }
}

__attribute__((noreturn))
void Default_Handler(void) {
    while (1) {
        __asm__ volatile ("bkpt #0");
    }
}
```

Section name `.vectors` matches the SDK's `memmap_default.ld`. (Your own script in
Exercise 3 will use `.isr_vector`; the SDK's name is different. Read the script.)

</details>

<details>
<summary>The application (`main.c`)</summary>

```c
/* main.c — minimal test for the startup file.
 *
 * Proves .data and .bss are correctly initialized before main() runs by
 * reading two globals and printing them over UART0 at 115200 8N1.
 */

#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

/* Initialized global — should read as 0x12345678 if .data copy worked. */
volatile uint32_t boot_counter = 0x12345678u;

/* Uninitialized global — should read as 0 if .bss zero worked. */
volatile uint32_t event_counter;

#define LED_PIN 15

int main(void) {
    stdio_init_all();
    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    /* Print the proof. */
    char buf[80];
    int n = snprintf(buf, sizeof buf,
        "crunch-wire w03 startup: boot_counter=0x%08lx event_counter=0x%08lx\r\n",
        (unsigned long)boot_counter,
        (unsigned long)event_counter);
    uart_write_blocking(uart0, (uint8_t *)buf, n);

    while (1) {
        gpio_put(LED_PIN, 1);
        sleep_ms(500);
        gpio_put(LED_PIN, 0);
        sleep_ms(500);
    }
    return 0;
}
```

The expected UART output, once per boot:

```
crunch-wire w03 startup: boot_counter=0x12345678 event_counter=0x00000000
```

If `boot_counter` reads as `0x00000000`, your `.data` copy did not run. If
`event_counter` reads as some garbage value (often `0xa5a5a5a5` or `0xaaaaaaaa`),
your `.bss` zero did not run. Both are first-day bugs.

</details>

<details>
<summary>The `CMakeLists.txt`</summary>

```cmake
cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(startup C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(startup startup.c main.c)

target_link_libraries(startup pico_stdlib)

# Exclude the SDK's crt0.S — we are providing our own startup.
target_link_options(startup PRIVATE -nostartfiles)

pico_enable_stdio_usb(startup 0)
pico_enable_stdio_uart(startup 1)

pico_add_extra_outputs(startup)
```

The `-nostartfiles` flag is the key line. Without it, the SDK's `crt0.S` is
linked as well as your `startup.c`, both define `__isr_vector` and
`Reset_Handler`, and the link fails with duplicate-symbol errors. With it, the
linker uses only your symbols.

The SDK's CMake otherwise builds normally — `pico_sdk_init()`, the linker
script `memmap_default.ld`, the UART driver, all of that. We are replacing
*only* the startup file.

</details>

<details>
<summary>Verifying the build</summary>

After build:

```bash
arm-none-eabi-nm -n -S build/startup.elf | grep -E 'Reset_Handler|__isr_vector|__StackTop|__data_start__'
arm-none-eabi-objdump -h build/startup.elf | head -15
arm-none-eabi-size build/startup.elf
```

Expected for `nm -n -S`:

```
10000100 000000c0 R __isr_vector
10000158 00000040 T Reset_Handler   # the address may vary by a few bytes
20000000 00000004 D boot_counter
20000004 00000004 B event_counter
20040000           A __StackTop
```

The `R` next to `__isr_vector` means read-only (in flash). The `T` next to
`Reset_Handler` means text (also in flash). The `D` next to `boot_counter`
means initialized data (in RAM, address `0x2000_0000` = the start of RAM).
The `B` next to `event_counter` means BSS (also in RAM, right after `.data`).
The `A` next to `__StackTop` means absolute (a constant the linker computed,
not associated with any section).

Expected for `objdump -h`:

```
Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .boot2        00000100  10000000  10000000  ...
  1 .vectors      000000c0  10000100  10000100  ...
  2 .text         00001234  100001c0  100001c0  ...
  3 .data         00000004  20000000  100013f4  ...
  4 .bss          00000004  20000004  20000004  ...
```

The `.data` section has VMA `0x2000_0000` and LMA in flash (different addresses).
The `.bss` section has VMA and LMA equal (NOLOAD; no LMA bytes in the file).

If you see `.vectors` at a non-`0x1000_0100` address, your linker script is
not aligning correctly. Try a clean rebuild.

</details>

<details>
<summary>Debugging — what to check if it does not boot</summary>

Symptom: UART silent, GP15 not toggling.

1. Check that `arm-none-eabi-nm -n build/startup.elf | head` shows `__isr_vector` at `0x1000_0100`. If it is at `0x1000_0000` or some other address, your linker is not placing the vector table after `boot2`.
2. Check that `objdump -h` shows `.vectors` section size = `0xc0` (192 bytes = 48 × 4). If it is `0x00`, your `__attribute__((section(".vectors"), used))` is wrong (typo, missing `used`, missing `KEEP()` in the linker script's `.vectors` block).
3. Check that the boot ROM is finding `boot2` correctly: re-flash a known-good `pico-sdk` example (the SDK's `blink`) and confirm the board still boots normally. If not, the board is in a weird state; press BOOTSEL+reset to re-enter BOOTSEL.
4. Open the `.uf2` with `picotool info -a build/startup.uf2` and confirm the load address is `0x1000_0000`. If it is something else, the `.uf2` conversion went wrong.
5. Last resort: comment out everything in `Reset_Handler` except `main()` (no `.data` copy, no `.bss` zero). If the UART now works (with `boot_counter` showing garbage), your `.data`/`.bss` symbol resolution is broken — re-check `extern uint32_t __etext, __data_start__, ...`. The symbol names must *exactly* match the linker script.

Symptom: UART prints garbage characters.

This is almost always a baud-rate mismatch. The SDK's `uart_init(uart0, 115200)` computes the divisor from `clk_peri`, which is at its boot default unless you bring up the PLL. The SDK's default initialization brings up the PLL to 125 MHz; if you have not called `pico_init()`, `clk_peri` is at the XOSC value (6 MHz), and your actual baud rate is `115200 × (6/125) = 5530 baud`. Set your terminal to 5530 baud — or, properly, call `pico_init()` early in `main()`.

</details>

## What to capture

In `notes/startup-walkthrough.md`, the structure:

```
# Exercise 2 — Minimal Startup

## What I wrote

- startup.c: <number> lines.
- The vector table has 48 entries; slots 4-10, 12-13, 42-47 are zero.
- Reset_Handler is <number> lines of C, runs the .data copy and .bss zero.

## Section layout (objdump -h)

[paste of the first 6 section headers, with VMA/LMA columns highlighted]

## Symbol map (nm -n -S, first 20)

[paste of the first 20 symbols, sorted by address]

## UART proof

Screenshot:
crunch-wire w03 startup: boot_counter=0x12345678 event_counter=0x00000000

The `boot_counter` value confirms .data was copied from flash to RAM.
The `event_counter` value (0) confirms .bss was zeroed.

## Reflection

Two paragraphs:
1. What was the first thing that did not work and why?
2. What would change if you removed the .bss zero loop? (Answer: event_counter
   would read whatever value was in SRAM at boot — usually nonzero, sometimes
   `0xa5a5a5a5` patterns from the boot ROM.)
```

## Stretch goals

- Add a second test: a `static const uint32_t magic = 0xdeadbeef;` at file scope. Confirm it ends up in `.rodata` (which is merged into `.text` on Cortex-M, so it lives in flash). Add a line to the UART output reading and printing `magic`. Verify it reads as `0xdeadbeef` and that `nm` reports it inside `0x1000_xxxx`.
- Trip the `Default_Handler` deliberately: in `main()`, after the UART print, write `*(volatile uint32_t *)0xe000ed1cu = 0x10000000u;` (set `ICSR.NMIPENDSET`, the pend-NMI bit). The CPU will vector to `NMI_Handler`, which is aliased to `Default_Handler`, which executes `bkpt #0`. The blink loop never starts. This proves the alias mechanism works.
- Replace `Default_Handler` with one that, before hanging, writes the active exception number (from the `IPSR` register, `0xe000_ed04`) to a known SRAM address (e.g. `0x2000_3ff0`). Soft-reset the chip after, and on the next boot, read that address back over UART. This is the kernel of a fault-recorder; we will build a real one in Week 7.

## Why this matters

This is the smallest possible end-to-end demonstration that you understand the C runtime on bare metal. Every later week of C7 — Modern C++, Rust, RTOS, USB, secure boot — assumes you can answer "what runs before `main()`?" in three bullet points. By Sunday you should be able to: (1) load SP from vector table; (2) copy `.data`; (3) zero `.bss`; (4) call `main()`.

If you can write `startup.c` from a blank file by Sunday, you understand bare-metal embedded better than 80% of working embedded engineers. If you cannot, do this exercise again until you can.
