# Challenge 1 — Bare-Bare-Metal Blink

**Time estimate:** ~150 minutes (most of it OpenOCD configuration; ~20 min of coding).

## Problem statement

Build a Pi Pico W blink firmware that runs entirely from SRAM, with **no `boot2`**, **no flash**, and **no XIP**. The firmware is loaded over SWD using `openocd` (or `probe-rs`), placed in SRAM at `0x2000_0000`, and started by writing the appropriate values to `VTOR` and `SP`. The firmware is volatile — it is gone on power-cycle — but the build is dramatically simpler than the flash-resident version, and you exercise an entirely different path through the linker script.

This is the smallest possible Cortex-M0+ firmware that does anything useful: ~1 KiB of `.text`, no second-stage bootloader, no QSPI configuration. It is also the production path for *some* real products — pre-silicon bring-up, debug-only validation firmware, JTAG-loaded factory test routines.

## Acceptance criteria

- [ ] A new directory `c7-week03-sram-blink/` containing:
  - `pico_sram.ld` — a linker script whose `FLASH` region is replaced by an SRAM-resident `RAM` region of 256 KiB at `0x2000_0000`. No `.boot2` section.
  - `startup.c` — your startup file, slightly modified (the `.data` copy is a no-op or removed, since `.data` is already in RAM).
  - `main.c` — same blink as Exercise 3.
  - `Makefile` — produces `build/blink_sram.elf` only (no `.uf2`).
  - `openocd.cfg` — configuration for your SWD probe (Picoprobe, J-Link EDU Mini, or CMSIS-DAP).
- [ ] Connect your SWD probe to the Pi Pico W's SWD pads (GND, SWDIO, SWCLK; pads at the bottom-center of the board, datasheet p. 11 of the Pico W datasheet).
- [ ] `openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "program build/blink_sram.elf verify reset exit"` flashes the firmware to SRAM and starts it.
- [ ] GP15 blinks at 1 Hz. The blink continues until you press the physical RESET button on the board — at which point the firmware vanishes (SRAM is volatile).
- [ ] A `notes/sram-blink.md` documents:
  - The linker script diff against your Exercise 3 `pico.ld` (3–4 lines changed).
  - The `openocd.cfg` you used and a one-paragraph explanation of each directive.
  - A scope screenshot showing GP15 at 1 Hz, with a label noting "running from SRAM, no flash."
  - One paragraph on the trade-offs: when is an SRAM-only firmware the right answer?

## Hints

<details>
<summary>The linker script changes</summary>

Starting from your Exercise 3 `pico.ld`, the changes:

```diff
 MEMORY {
-    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 2M
-    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 256K
+    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 256K
 }

 SECTIONS {
-
-    .boot2 : {
-        KEEP(*(.boot2))
-        . = ALIGN(256);
-    } > FLASH
-
-    .isr_vector : ALIGN(256) {
+    .isr_vector : ALIGN(256) {
         __isr_vector = .;
         KEEP(*(.isr_vector))
-    } > FLASH
+    } > RAM

     .text : {
         *(.text*)
         *(.rodata*)
         . = ALIGN(4);
         _etext = .;
-    } > FLASH
+    } > RAM

     _sidata = LOADADDR(.data);
     .data : {
         . = ALIGN(4);
         _sdata = .;
         *(.data*)
         . = ALIGN(4);
         _edata = .;
-    } > RAM AT > FLASH
+    } > RAM AT > RAM
     /* Note: VMA == LMA, so the Reset_Handler's .data copy is a no-op
      * (src == dst). The loop still runs but does nothing visible. */

     .bss (NOLOAD) : { ... } > RAM     /* unchanged */
 }
```

Net effect: ~6 lines changed, `.text` and `.data` LMA both move to RAM,
`.boot2` deleted. The linker script becomes ~50 lines.

You may also adjust `_estack` if you want to leave the top 16 KiB of SRAM
for OpenOCD's working area:

```ld
_estack = ORIGIN(RAM) + LENGTH(RAM) - 16K;
```

This is optional; OpenOCD's default scratch area is 4 KiB at the top of RAM,
which it relocates automatically.

</details>

<details>
<summary>The OpenOCD configuration</summary>

You need a SWD probe. Options:

- **Picoprobe** — a second Pi Pico W flashed with the `debugprobe` firmware. Cost: another $6 Pico. <https://github.com/raspberrypi/debugprobe>
- **CMSIS-DAP probe** — any clone (~$15 on Amazon).
- **J-Link EDU Mini** — $20, requires SEGGER's `JLinkGDBServer`, not OpenOCD.

Wire the probe to your target Pi Pico W:

```
   Probe    Target Pico W
   -----    -------------
   GND  --- GND
   SWDIO -- SWDIO  (lower-left of board, marked DEBUG)
   SWCLK -- SWCLK
   VTREF -- 3V3 (some probes need this for level shifting)
```

OpenOCD configuration (`openocd.cfg`):

```
# Use the CMSIS-DAP probe.
source [find interface/cmsis-dap.cfg]
adapter speed 5000

# Target: RP2040.
source [find target/rp2040.cfg]

# Don't try to verify against flash; we are SRAM-resident.
set USE_CORE 0
```

Or, with a Picoprobe (the official Raspberry Pi debug probe firmware):

```
source [find interface/cmsis-dap.cfg]
adapter speed 5000
transport select swd
source [find target/rp2040.cfg]
```

Then:

```bash
$ openocd -f openocd.cfg -c "program build/blink_sram.elf verify reset exit"
```

OpenOCD halts the CPU, writes the ELF sections into SRAM, sets `VTOR` to the
vector table's address, writes `SP` from word 0 of the vector table, writes
`PC` from word 1, and resumes. The blink starts immediately.

</details>

<details>
<summary>Why the .data copy is a no-op now</summary>

In Exercise 3, your `.data` has VMA in RAM and LMA in flash. The Reset_Handler
copies LMA -> VMA at boot, so the initial values of globals are loaded.

In this SRAM-only build, your `.data` has VMA in RAM *and* LMA in RAM —
both at the same address. The linker writes the initial values directly into
RAM (as the OpenOCD load programs the ELF). The Reset_Handler's copy loop
runs, but `src == dst` and the loop is effectively `while (dst < &_edata)
*dst++ = *dst++;` — a no-op.

You can keep the loop or remove it; removing it shaves three instructions but
makes the startup file diverge between flash-resident and SRAM-resident
builds, which is annoying. Most production codebases keep the loop and
accept the cost.

The `.bss` zero loop, by contrast, *is* still needed. OpenOCD does *not* zero
the `.bss` region as part of the load — the ELF says NOLOAD, meaning "no
bytes in the file" — so SRAM is left at whatever pattern was there before.
Without the `.bss` zero loop, your globals would start at garbage values.

</details>

<details>
<summary>The `Makefile` changes</summary>

Starting from Exercise 3's Makefile, the changes:

```diff
-LDFLAGS = -T pico.ld ...
+LDFLAGS = -T pico_sram.ld ...

-$(BUILD)/blink.elf: $(BUILD)/boot2.o $(C_OBJS) pico.ld | $(BUILD)
-       $(LD) $(LDFLAGS) -o $@ $(BUILD)/boot2.o $(C_OBJS)
+$(BUILD)/blink_sram.elf: $(C_OBJS) pico_sram.ld | $(BUILD)
+       $(LD) $(LDFLAGS) -o $@ $(C_OBJS)
+
+flash: $(BUILD)/blink_sram.elf
+       openocd -f openocd.cfg -c "program $< verify reset exit"
+
+# No UF2 target; SRAM-resident firmware is not flashable.
```

Delete the `boot2*` targets and the `elf2uf2`/`.bin`/`.uf2` targets. The build
becomes:

```
1. arm-none-eabi-gcc -c startup.c main.c
2. arm-none-eabi-ld -T pico_sram.ld -o build/blink_sram.elf
3. openocd ... program build/blink_sram.elf
```

Three steps. The whole `make` runs in about 0.5 seconds.

</details>

<details>
<summary>What happens when you press the physical RESET button</summary>

Pressing RUN (the active-low reset pin on the Pi Pico W) does a hard reset of
the RP2040. The boot ROM runs, reads the first 256 bytes of flash — which is
unchanged from whatever was there before (likely a `pico-sdk` blink left over
from Week 1 or Week 2, or a previous flash) — verifies its CRC, jumps into it.
If the flash is empty or corrupted, the boot ROM drops into BOOTSEL mode.
*Either way, your SRAM-resident firmware is gone.* SRAM is volatile.

This is the headline trade-off for SRAM-only firmware: it does not survive a
power cycle or a reset. For a production part this is unacceptable; for
factory test, pre-silicon validation, or debugger-driven bring-up it is fine.

To "re-flash" your SRAM firmware: re-run `openocd -c "program ... verify
reset exit"`. The probe halts the CPU, reloads, restarts. ~2 seconds.

</details>

## What to capture

`notes/sram-blink.md`:

```
# Challenge 1 — SRAM-resident blink

## Linker script diff vs Exercise 3

[6-line diff: FLASH region removed, .boot2 section removed, .text now > RAM]

## Toolchain

- arm-none-eabi-gcc 13.2.Rel1
- arm-none-eabi-ld 2.41
- openocd 0.12.0
- probe: <Picoprobe / CMSIS-DAP clone / J-Link EDU Mini>

## openocd.cfg

[paste]

## Build output

[paste of `make` showing the 3-step build]

## Bench verification

[scope or Saleae screenshot of GP15 toggling at 1 Hz, labeled "SRAM-resident"]

## Trade-off analysis

One paragraph:
- When is an SRAM-only firmware the right answer?
  (Factory test, pre-silicon bring-up, debugger-driven validation,
   single-step debugging of a startup file before flash is even functional.)
- When is it the wrong answer?
  (Any production firmware. Any firmware that must survive a power-cycle.
   Any firmware that ships to an end user.)
```

## Stretch goals

- Use `probe-rs` instead of OpenOCD. `cargo install probe-rs`, then `probe-rs run --chip RP2040 build/blink_sram.elf`. Compare ergonomics. `probe-rs` is faster to start (no GDB server) and has nicer error messages; OpenOCD is more configurable. Document your preference.
- Build the firmware with `-DRUN_FROM_SRAM` and add a compile-time conditional so the same source tree builds both flash-resident and SRAM-resident binaries. The linker scripts diverge but the `startup.c` and `main.c` are shared. ~30 lines of `#ifdef`.
- Single-step through your `Reset_Handler` with `arm-none-eabi-gdb` over OpenOCD. `target extended-remote :3333`, then `monitor reset halt`, then `b Reset_Handler`, then `c`. Watch the `.data` copy loop execute (well, it is a no-op for the SRAM build), then the `.bss` zero loop, then the call to `main()`. This is the first time most students *see* the startup file run.
- Tweak the `_estack` value to leave room for OpenOCD's scratch. Default: 4 KiB at the top of RAM. Try 16 KiB. Confirm OpenOCD still works. Document.

## Why this matters

Most Cortex-M-class chips have a similar option: load firmware to SRAM over JTAG/SWD without touching flash. On parts where flash is slow to program (NAND-backed XIP, eMMC), the SRAM-loaded variant is the fast iteration loop. On parts in early bring-up (engineering samples, pre-tapeout silicon), the SRAM-only path is the only path that works.

The bigger lesson here is the *separation of concerns* in the boot chain: flash configuration, vector-table location, `.data` copy, runtime init are each independent decisions. You can mix and match. You can put `.text` in flash and `.data` in SRAM (the normal case). You can put both in SRAM (this challenge). You can put `.text` in flash and `.data` in CCM (the STM32F4 high-performance case). You can put `.text` in SRAM and `.rodata` in flash. Each combination is a few lines of linker-script change. By Sunday you know how.

A C7 graduate at Week 3 can author a linker script for any Cortex-M target in 30 minutes. By Week 24 you will have done this for at least three families (RP2040, STM32F4, nRF52). The muscle starts here.
