# Exercise 3 — Blink Without the SDK

**Time estimate:** ~180 minutes (most of it the `Makefile`; ~30 min of debugging the first boot).

## Problem statement

Combine the linker script from Lecture 2, the startup file from Exercise 2, the borrowed `boot2_w25q080.S` from `pico-sdk`, and your own `main.c` into a Pi Pico W blink firmware that uses **zero** `pico-sdk` source files in the tree (only the `boot2` assembly file, which we explicitly call out as a borrowed-from-vendor artifact). Build it with a `Makefile` that invokes `arm-none-eabi-gcc` and `arm-none-eabi-ld` directly — no CMake.

The output `.uf2` flashes via BOOTSEL, blinks GP15 at 1 Hz, and your `git log` contains no SDK code. This is the Week 3 keystone.

## Acceptance criteria

- [ ] A new directory `c7-week03-bare-blink/` containing:
  - `pico.ld` — your linker script (from Lecture 2)
  - `startup.c` — your startup file (from Exercise 2, adapted to your own script's symbol names)
  - `main.c` — the blink + UART log
  - `boot2_w25q080.S` — borrowed from `pico-sdk`, with the source path and commit hash documented in a comment at the top
  - `pad_checksum.py` — borrowed from `pico-sdk`, with the source path documented
  - `Makefile` — your build, no CMake
- [ ] `make` (zero extra arguments) produces `build/blink.uf2` and `build/blink.elf` on a clean clone.
- [ ] `picotool info -a build/blink.uf2` shows: load address `0x10000000`, size ≤ 4 KiB.
- [ ] The firmware, flashed via BOOTSEL, blinks GP15 at 1.0 Hz ± 1% indefinitely and prints `"crunch-wire w03 boot ok"` over UART0 at 115200 8N1 once per second.
- [ ] `find . -name '*.c' -o -name '*.h' | xargs grep -l 'pico_sdk\|hardware_uart\|hardware_gpio'` returns **no matches** — no SDK headers, no SDK calls. Only `boot2_w25q080.S` is a borrowed artifact, explicitly noted.
- [ ] A `MEMORY-MAP.md` documents your linker script's regions and the symbols it exports. (This is the seed of the mini-project deliverable.)
- [ ] A `notes/build-log.md` captures the output of `make clean && make V=1` so a reviewer can read every command issued.

## Hints

<details>
<summary>The directory layout</summary>

```
c7-week03-bare-blink/
├── Makefile
├── pico.ld
├── startup.c
├── main.c
├── boot2_w25q080.S         # borrowed from pico-sdk (cite path + commit)
├── pad_checksum.py         # borrowed from pico-sdk (cite path + commit)
├── elf2uf2/                # borrowed prebuilt or built from source
├── MEMORY-MAP.md
└── notes/
    ├── build-log.md
    └── boot-verification.md
```

</details>

<details>
<summary>The `Makefile`</summary>

```makefile
# C7 · Crunch Wire — Week 03 — bare-metal RP2040 blink Makefile

CROSS    = arm-none-eabi
CC       = $(CROSS)-gcc
LD       = $(CROSS)-ld
AS       = $(CROSS)-as
OBJCOPY  = $(CROSS)-objcopy
OBJDUMP  = $(CROSS)-objdump
SIZE     = $(CROSS)-size

CFLAGS  = -mcpu=cortex-m0plus -mthumb \
          -ffreestanding -fno-builtin -nostdlib \
          -ffunction-sections -fdata-sections \
          -O2 -g -Wall -Wextra

LDFLAGS = -T pico.ld \
          -Map=build/blink.map \
          --gc-sections \
          -nostdlib

BUILD = build

C_SRCS = startup.c main.c
C_OBJS = $(addprefix $(BUILD)/, $(C_SRCS:.c=.o))

.PHONY: all clean

all: $(BUILD)/blink.uf2 $(BUILD)/blink.bin

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# Build boot2: assemble, link, extract, pad with CRC, re-wrap as .o
$(BUILD)/boot2_raw.o: boot2_w25q080.S | $(BUILD)
	$(CC) -mcpu=cortex-m0plus -mthumb -c $< -o $@

$(BUILD)/boot2_raw.elf: $(BUILD)/boot2_raw.o | $(BUILD)
	$(LD) -nostdlib --entry _stage2_boot -Ttext=0x20041f00 $< -o $@

$(BUILD)/boot2_raw.bin: $(BUILD)/boot2_raw.elf | $(BUILD)
	$(OBJCOPY) -O binary $< $@

$(BUILD)/boot2.bin: $(BUILD)/boot2_raw.bin | $(BUILD)
	python3 pad_checksum.py -s 0xffffffff $< $@

$(BUILD)/boot2.o: $(BUILD)/boot2.bin | $(BUILD)
	$(OBJCOPY) -I binary -O elf32-littlearm -B armv6-m \
	  --rename-section .data=.boot2,alloc,load,readonly,code,contents \
	  $< $@

$(BUILD)/blink.elf: $(BUILD)/boot2.o $(C_OBJS) pico.ld | $(BUILD)
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/boot2.o $(C_OBJS)
	$(SIZE) $@

$(BUILD)/blink.bin: $(BUILD)/blink.elf | $(BUILD)
	$(OBJCOPY) -O binary $< $@

$(BUILD)/blink.uf2: $(BUILD)/blink.elf | $(BUILD)
	elf2uf2 $< $@

clean:
	rm -rf $(BUILD)
```

A few notes:

- **`elf2uf2`** is the SDK's `elf2uf2` tool. If you do not have it, you can clone `pico-sdk/tools/elf2uf2/`, run `cmake -B build . && cmake --build build`, and put the resulting binary on your `$PATH`. Alternatively, use `picotool` directly (`picotool load -f -x build/blink.elf -t elf`). Document your choice.
- **`pad_checksum.py`** is the SDK's Python script that appends the 4-byte CRC to `boot2.bin`. Copy it from `pico-sdk/src/rp2_common/boot_stage2/pad_checksum`. About 50 lines. The `-s 0xffffffff` flag is the seed for the CRC polynomial.
- **The `boot2.o` re-wrap step** is the tricky one. `arm-none-eabi-objcopy -I binary` takes a raw `.bin` and wraps it as a single `.data` section in an ELF object. We then rename the section to `.boot2` so the linker script's `KEEP(*(.boot2))` will pick it up.

</details>

<details>
<summary>The `main.c` — no SDK</summary>

```c
/* main.c — C7 · Crunch Wire — Week 03 — bare-metal blink.
 *
 * No pico-sdk. Direct register writes only. Citations to the RP2040 datasheet
 * (Sep-2024 rev) for every magic number.
 */

#include <stdint.h>

/* RP2040 register addresses we use. */
#define RESETS_BASE         0x4000c000u
#define RESETS_RESET        (RESETS_BASE + 0x00u)   /* §2.14.3 p.219 */
#define RESETS_RESET_DONE   (RESETS_BASE + 0x08u)   /* §2.14.3 p.219 */

#define IO_BANK0_BASE       0x40014000u
#define IO_BANK0_GPIO15_CTRL  (IO_BANK0_BASE + 0x04u + 15u * 0x08u)  /* §2.19.6.1 p.247 */
#define IO_BANK0_GPIO0_CTRL   (IO_BANK0_BASE + 0x04u + 0u * 0x08u)
#define IO_BANK0_GPIO1_CTRL   (IO_BANK0_BASE + 0x04u + 1u * 0x08u)

#define PADS_BANK0_BASE     0x4001c000u
#define PADS_GPIO15         (PADS_BANK0_BASE + 0x04u + 15u * 0x04u)  /* §2.19.4 p.294 */

#define SIO_BASE            0xd0000000u
#define SIO_GPIO_OE_SET     (SIO_BASE + 0x024u)     /* §2.3.1.7 p.43 */
#define SIO_GPIO_OUT_XOR    (SIO_BASE + 0x01cu)     /* §2.3.1.7 p.42 */

#define UART0_BASE          0x40034000u
#define UART0_DR            (UART0_BASE + 0x00u)    /* §4.2.7.1 p.432 */
#define UART0_FR            (UART0_BASE + 0x18u)    /* §4.2.7.3 p.434 */
#define UART0_IBRD          (UART0_BASE + 0x24u)    /* §4.2.7.4 p.435 */
#define UART0_FBRD          (UART0_BASE + 0x28u)    /* §4.2.7.5 p.435 */
#define UART0_LCR_H         (UART0_BASE + 0x2cu)    /* §4.2.7.6 p.436 */
#define UART0_CR            (UART0_BASE + 0x30u)    /* §4.2.7.7 p.437 */

/* Atomic alias offsets (RP2040 datasheet §2.1.2 p.18). */
#define ATOMIC_CLR  0x3000u

#define REG(addr)   (*(volatile uint32_t *)(addr))

#define BIT(n)      (1u << (n))

/* Resource bits in RESETS_RESET (§2.14.3 p.219). */
#define RESETS_IO_BANK0     BIT(5)
#define RESETS_PADS_BANK0   BIT(8)
#define RESETS_UART0        BIT(22)

static void unreset(uint32_t mask) {
    /* Clear the reset bits via the atomic CLR alias. */
    REG(RESETS_RESET + ATOMIC_CLR) = mask;
    /* Wait until DONE bits are set for the resources we un-reset. */
    while ((REG(RESETS_RESET_DONE) & mask) != mask) { }
}

static void gpio15_output_init(void) {
    REG(IO_BANK0_GPIO15_CTRL) = 5u;           /* FUNCSEL = 5 (SIO) */
    REG(PADS_GPIO15) = (1u << 6) | (2u << 4); /* IE=1, DRIVE=2 (8mA) */
    REG(SIO_GPIO_OE_SET) = BIT(15);
}

static void uart0_init_115200(void) {
    /* clk_peri defaults to XOSC (12 MHz) at this stage if we have not brought
     * up the PLL. The boot ROM has already configured XOSC and clk_peri
     * for us (RP2040 §2.8.1.4 p.133); we trust it.
     *
     * Divisor = clk_peri / (16 * baud) = 12_000_000 / (16 * 115200) = 6.510
     * IBRD = 6, FBRD = round(0.510 * 64) = 33.
     */
    REG(UART0_IBRD) = 6;
    REG(UART0_FBRD) = 33;
    REG(UART0_LCR_H) = (3u << 5) | (1u << 4);  /* 8N1, FIFO enable */
    REG(UART0_CR) = BIT(0) | BIT(8) | BIT(9);  /* UARTEN, TXE, RXE */
    /* Pinmux GP0/GP1 to UART0. */
    REG(IO_BANK0_GPIO0_CTRL) = 2u;
    REG(IO_BANK0_GPIO1_CTRL) = 2u;
}

static void uart0_putc(char c) {
    /* Wait for TX FIFO not-full. UARTFR bit 5 = TXFF. */
    while (REG(UART0_FR) & BIT(5)) { }
    REG(UART0_DR) = (uint8_t)c;
}

static void uart0_puts(const char *s) {
    while (*s) uart0_putc(*s++);
}

/* Crude delay; calibrate by measuring on the scope. Not for production. */
static void delay_loop(volatile uint32_t n) {
    while (n--) { __asm__ volatile (""); }
}

int main(void) {
    /* Bring needed peripherals out of reset. */
    unreset(RESETS_IO_BANK0 | RESETS_PADS_BANK0 | RESETS_UART0);

    gpio15_output_init();
    uart0_init_115200();

    /* Tight blink loop with a UART line per cycle.
     * 500 ms half-period at clk_sys ≈ 12 MHz, ~3 cycles/iter -> ~2_000_000. */
    while (1) {
        REG(SIO_GPIO_OUT_XOR) = BIT(15);
        uart0_puts("crunch-wire w03 boot ok\r\n");
        delay_loop(2000000);
    }
    return 0;
}
```

The `delay_loop` is the laziest possible timer — we will replace it with SysTick in Week 7. For Week 3 the bench measurement (use the scope to confirm 1 Hz) is the verification. If the loop runs faster than 1 Hz, increase the count. If it runs slower, decrease.

</details>

<details>
<summary>Inspecting the result</summary>

After `make`:

```bash
$ make
$ arm-none-eabi-size build/blink.elf
   text    data     bss     dec     hex filename
   1056      16      32    1104     450 build/blink.elf

$ picotool info -a build/blink.uf2
File size:        1024 bytes
Load address:     0x10000000
Drag and drop:    target /Volumes/RPI-RP2
Boot 2 type:      W25Q080

$ arm-none-eabi-nm -n -S build/blink.elf | head -10
10000000 00000100 t _stage2_boot
10000100 000000c0 R __isr_vector
100001c0 ...      T Reset_Handler
100001fc ...      T main
...
20000000 ...      B _sbss
2004_0000 .       A _estack
```

The `_stage2_boot` symbol at `0x1000_0000` is from `boot2_w25q080.S`. The vector table is at `0x1000_0100`. Your code follows.

Flash:
```bash
$ picotool load -f build/blink.uf2
$ picotool reboot
```

Open `screen /dev/tty.usbserial-* 115200` (or `picocom`, or `minicom`) and you should see one `crunch-wire w03 boot ok` line per second. GP15 toggles in sync; verify with a scope or LED.

</details>

<details>
<summary>Debugging — common failures</summary>

**Symptom: BOOTSEL works but the chip does not boot to your firmware.**
The most likely cause is a bad `boot2` CRC. Re-run `pad_checksum.py` and confirm the resulting `.bin` is exactly 256 bytes. `wc -c build/boot2.bin` should print `256`.

**Symptom: `picotool info` shows "Load address: 0x10000000" but no `.uf2` magic.**
You probably ran `elf2uf2` against a `.bin` instead of an `.elf`. Re-check the Makefile target. `.uf2` is generated from the linked ELF, not the raw binary.

**Symptom: chip reboots to BOOTSEL repeatedly.**
The boot ROM rejected your `boot2` (CRC failed). It then drops into BOOTSEL. Re-verify `pad_checksum.py` ran. Or: your firmware HardFaulted in `Reset_Handler` before clearing the watchdog. Comment out the `.data` copy loop, rebuild, and see if it boots — if yes, your loop has a bug (usually a typo in the linker-script symbol names).

**Symptom: UART silent but LED blinks.**
Your `clk_peri` is at the wrong frequency. The boot ROM brings up XOSC to 12 MHz on the Pi Pico W; if your divisor math assumed 125 MHz, your actual baud is `115200 × (12/125) ≈ 11000`. Either fix the divisor for 12 MHz, or bring up the PLL_SYS in `main()` first (datasheet §2.18 p.234) — out of scope this week.

**Symptom: UART prints garbage characters.**
Wrong baud rate at the terminal, or wrong divisor in firmware. Check both. A common slip: `IBRD = 67` and `FBRD = 52` assume `clk_peri = 125 MHz`, but in a no-SDK build the clock is 12 MHz unless you set it.

</details>

<details>
<summary>The `MEMORY-MAP.md` seed</summary>

```markdown
# c7-week03-bare-blink — Memory Map

Pi Pico W, RP2040, arm-none-eabi-gcc 13.2.Rel1, GNU ld 2.41

## Regions defined in pico.ld

| Region | Origin | Length | Attributes | Notes |
|--------|--------|--------|------------|-------|
| FLASH  | `0x10000000` | 2 MiB | (rx) | XIP into W25Q080 QSPI flash; §2.2 p.23 |
| RAM    | `0x20000000` | 256 KiB | (xrw) | Striped SRAM0–SRAM3; §2.2 p.23 |

## Output sections placed by pico.ld

| Section | VMA | LMA | Purpose |
|---------|-----|-----|---------|
| .boot2 | `0x10000000` | `0x10000000` | 256-byte second-stage bootloader, CRC-checked |
| .isr_vector | `0x10000100` | `0x10000100` | 48-entry Cortex-M0+ vector table |
| .text | `0x100001c0` | `0x100001c0` | Code, .rodata, init array |
| .data | `0x20000000` | `0x100014ec` | Initialized globals; copied at boot |
| .bss | `0x20000010` | (none) | Zero-initialized globals; zeroed at boot |

## Symbols exported

| Symbol | Address | Used by |
|--------|---------|---------|
| _estack | `0x20040000` | Vector table word 0 |
| __isr_vector | `0x10000100` | Boot ROM reads this for SP/PC |
| Reset_Handler | `0x100001c0` | Boot ROM jumps here after boot2 |
| _sidata | `0x100014ec` | Reset_Handler reads from here |
| _sdata, _edata | `0x20000000`, `0x20000010` | Reset_Handler writes here |
| _sbss, _ebss | `0x20000010`, `0x20000030` | Reset_Handler zeroes here |

## Size

| Section | Bytes |
|---------|------:|
| .text | 1056 |
| .data | 16 |
| .bss | 32 |
| Total flash use | ~1.4 KiB |
| Free flash | ~2046 KiB |
```

You will polish this into the mini-project's `MEMORY-MAP.md`. Start it here.

</details>

## What to capture

In `notes/build-log.md`:

- The full output of `make clean && make V=1`. Every command, every flag.
- A confirmation: `grep -l 'pico_sdk\|hardware_uart\|hardware_gpio' *.c *.h` returns no matches.
- The output of `arm-none-eabi-size build/blink.elf` (expect: total ~1.2 KiB).
- The output of `picotool info -a build/blink.uf2`.

In `notes/boot-verification.md`:

- A scope or Saleae screenshot of GP15 at 1 Hz.
- A Saleae screenshot (or terminal text) of the UART line at 115200 8N1.
- One paragraph: "What was the first thing that did not work, and how did you fix it?"

## Stretch goals

- Bring up `PLL_SYS` to 125 MHz in `main()` before the UART init. Read RP2040 datasheet §2.18 (PLL), p. 234. The sequence is: enable XOSC, configure PLL_SYS divisors, wait for PLL lock, set `clk_sys` source to PLL_SYS. ~30 lines of register writes. Recompute the UART divisor for `clk_peri = 125 MHz`. The resulting firmware boots ~10× faster than the boot-default version.
- Add a `make flash` target that calls `picotool load -f build/blink.uf2 && picotool reboot`. The full build-and-flash cycle should take < 5 seconds end-to-end.
- Add a HardFault test: in `main()`, after the LED comes on but before the UART init, dereference a deliberately wrong pointer like `*(volatile uint32_t *)0xffffffffu = 0;`. The chip HardFaults. Your `Default_Handler` traps. Confirm with `arm-none-eabi-gdb` over SWD that the CPU is stuck in `Default_Handler`.
- Strip the firmware: `arm-none-eabi-strip --strip-debug build/blink.elf` and rebuild the `.uf2`. Compare sizes. Document the savings.

## Why this matters

This is the artifact a Week 4 reviewer will reflash to confirm you understand the boot chain. By Sunday, your `git log` shows the linker script, the startup file, the boot2 borrow, and your own application. No SDK in your tree. The blink works. You wrote every byte of the boot chain.

That fluency unlocks the rest of C7. Week 4 (C++ on bare metal) will replace `startup.c` with `startup.cpp`. Week 5 (Rust) will replace it again with `cortex-m-rt` annotations. Week 9 (Bootloaders) will modify the linker script to carve a bootloader region. Each of those weeks builds on the muscle you build here.

If you cannot reproduce this exercise from a blank directory in 30 minutes by Sunday, do it again next week. It is the gate.
