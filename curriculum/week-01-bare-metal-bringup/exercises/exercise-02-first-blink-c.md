# Exercise 2 — First Blink in C

**Time estimate:** ~90 minutes (most of it the first CMake build).

## Problem statement

Build a C blink program for the Raspberry Pi Pico W using the official `pico-sdk`, flash it two different ways, and verify the LED on `WL_GPIO0` toggles at 1 Hz. The LED is wired to the CYW43439 wireless module, not to a normal GPIO — this is the single most common Week 1 trap, and we hit it on purpose.

## Acceptance criteria

- [ ] A new directory `c7-week01-blink-c/` containing `blink.c`, `CMakeLists.txt`, and `pico_sdk_import.cmake`.
- [ ] `cmake -B build -DPICO_BOARD=pico_w .` and `cmake --build build -j` complete with no warnings (other than the SDK's own).
- [ ] `build/blink.elf`, `build/blink.uf2`, `build/blink.bin`, and `build/blink.hex` exist.
- [ ] `arm-none-eabi-size build/blink.elf` is captured and committed.
- [ ] The Pi Pico W's on-board green LED blinks at **1.0 Hz ± 5%**, observed for at least 10 seconds, after flashing via the BOOTSEL drag-and-drop method.
- [ ] You re-flash the same `.elf` using `probe-rs run --chip RP2040 build/blink.elf` and observe the same blink. (If you do not have a debug probe, replace with a second BOOTSEL flash and document that you did not have a probe.)
- [ ] One scope or Saleae capture of the `WL_GPIO0` signal — or, if you cannot probe `WL_GPIO0` directly because it is inside the CYW43, a probe of any second GPIO you toggle alongside the LED.
- [ ] A `notes/blink-c.md` file capturing the four outputs (the build log, the `size` output, the BOOTSEL flash steps, and the scope/Saleae screenshot) in your Week 1 repo.

## Hints

<details>
<summary>The source file (`blink.c`)</summary>

```c
/* C7 · Crunch Wire — Week 01 — Blink the Pi Pico W on-board LED at 1 Hz.
 *
 * Hardware: Raspberry Pi Pico W
 * LED:      CYW43 WL_GPIO0 (via cyw43_arch_gpio_put — NOT a normal GPIO)
 * SDK:      pico-sdk >= 1.5.1
 *
 * Build:
 *   cmake -B build -DPICO_BOARD=pico_w .
 *   cmake --build build -j
 * Flash (BOOTSEL):
 *   hold BOOTSEL, plug USB, release BOOTSEL, drag build/blink.uf2 to RPI-RP2
 * Flash (probe-rs):
 *   probe-rs run --chip RP2040 build/blink.elf
 */

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

int main(void)
{
    /* Init the CYW43 driver in "no-extra-stack" mode.  This brings up the
     * SPI link to the wireless chip far enough to toggle WL_GPIO0; it does
     * NOT spin up Wi-Fi or BLE.  ROM cost: ~6 KB. */
    if (cyw43_arch_init() != 0) {
        /* CYW43 brought up no SPI handshake — board defect, bad solder, or
         * wrong PICO_BOARD. */
        return 1;
    }

    for (;;) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);   /* LED on  */
        sleep_ms(500);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);  /* LED off */
        sleep_ms(500);
    }
}
```

</details>

<details>
<summary>The CMakeLists.txt</summary>

```cmake
cmake_minimum_required(VERSION 3.13)

# Pull in the SDK's import helper before project() — it sets the toolchain.
include(pico_sdk_import.cmake)

project(blink C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# The board must be set BEFORE pico_sdk_init().
# pico_w  = Pi Pico W (CYW43 on board; LED behind it)
# pico    = original Pi Pico (LED on GP25, no wireless)
if (NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico_w)
endif ()

pico_sdk_init()

add_executable(blink blink.c)

target_link_libraries(blink
    pico_stdlib
    pico_cyw43_arch_none
)

# Generate .uf2, .bin, .hex alongside the .elf.
pico_add_extra_outputs(blink)
```

</details>

<details>
<summary>The `pico_sdk_import.cmake`</summary>

Copy this file directly from `$PICO_SDK_PATH/external/pico_sdk_import.cmake`. It is the SDK's bootstrap; do not edit it.

```bash
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .
```

</details>

<details>
<summary>The build steps</summary>

```bash
# Set the SDK path (if not already in your shell rc)
export PICO_SDK_PATH=$HOME/code/pico-sdk

# Configure the build
cmake -B build -DPICO_BOARD=pico_w .

# Build
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# Inspect the result
arm-none-eabi-size build/blink.elf
ls -la build/blink.*
```

Expected `size` output, ballpark:

```
   text    data     bss     dec     hex filename
  21344     324    8224   29892    74c4 build/blink.elf
```

The exact bytes vary across SDK versions; the order of magnitude is the signal. ~21 KB of `.text` for the CYW43 driver + your `main()` is normal. If your `.text` is below ~5 KB, you likely linked `pico_stdlib` only and not `pico_cyw43_arch_none`; the LED will not blink.

</details>

<details>
<summary>Flashing — the BOOTSEL drag-and-drop</summary>

1. Unplug the Pi Pico W.
2. Hold down the BOOTSEL button on the board.
3. While holding BOOTSEL, plug the USB cable into the laptop.
4. Release BOOTSEL ~1 second after plug-in.
5. A USB mass-storage drive named `RPI-RP2` appears on your laptop.
6. Open the `RPI-RP2` drive. You will see `INDEX.HTM` and `INFO_UF2.TXT` — read them once for fun.
7. Drag `build/blink.uf2` onto the `RPI-RP2` drive.
8. The drive unmounts within ~1 second. The board reboots into your firmware.
9. The on-board green LED begins blinking at 1 Hz.

If the LED does not blink:
- Confirm `PICO_BOARD=pico_w` (not `pico`). On a plain Pico, the LED is GP25; on a Pico W, the LED is `WL_GPIO0`. Wrong board → wrong driver → no LED.
- Confirm `cyw43_arch_init()` returned 0. Set a `gpio_put(15, 1)` early in `main()`, wire GP15 to a scope, and confirm the program reaches that line.
- Confirm the USB cable is a data cable (some are power-only).

</details>

<details>
<summary>Flashing — `probe-rs run`</summary>

If you have a `debugprobe` (a second Pico) or a J-Link, wire the SWD pins on the Pico W:

```
debugprobe Pico ───── target Pico W
   GP2  (SWCLK) ─────  SWCLK
   GP3  (SWDIO) ─────  SWDIO
   GND          ─────  GND
   3V3 (out)    ─────  3V3 (out)   ← share the 3V3 rail if powering the target
```

Then:

```bash
probe-rs run --chip RP2040 build/blink.elf
```

`probe-rs` halts the target, flashes, resets, and streams any RTT logs. For this exercise there are no logs; the command will sit waiting after "Running on chip…" Press Ctrl-C to exit. The LED keeps blinking.

</details>

<details>
<summary>Scope / Saleae capture</summary>

The LED itself is behind the CYW43 chip and not directly probeable. Two acceptable approaches:

1. **Probe the SPI traffic** between RP2040 and CYW43 (datasheet, Pico W p. 14 for the wiring). You will see a small SPI burst each 500 ms — the LED command. This is the "right" answer technically, but harder to interpret.
2. **Add a second toggle on a probeable GPIO**, alongside the LED, for the capture. Insert before the `sleep_ms`:

   ```c
   gpio_init(15);
   gpio_set_dir(15, GPIO_OUT);
   /* … in the loop … */
   gpio_put(15, true);  sleep_ms(500);
   gpio_put(15, false); sleep_ms(500);
   ```

   Wire GP15 to your scope or Saleae channel 0. You will see a clean 1 Hz square wave, 50% duty cycle.

Approach 2 is what most students do. Capture 5 seconds at 1 MHz sample rate. Save the file. Commit it.

</details>

## Why this matters

This is the first end-to-end firmware artifact you will produce. It is intentionally small — three files, ~30 lines of C — because every later week of C7 adds layers on top of exactly this. The bring-up note (Saturday's mini-project) requires this blink to be working before any of the other deliverables matter.

The deeper point: there is a long chain from your `blink.c` to a blinking LED — compiler, linker, BOOTSEL ROM, flash controller, CPU reset, CYW43 SPI handshake, GPIO pad — and every link in that chain is at some point going to break in your career. This week you get the chain working once, with a clean reference implementation. Next time it breaks, you have something to diff against.

## Submission

Commit `notes/blink-c.md` containing:

- The `size` output, verbatim.
- The terminal log of the build.
- A description of the flash method you used (BOOTSEL, probe-rs, or both).
- The scope / Saleae screenshot.
- One sentence on the first thing that went wrong, and how you fixed it.

When you submit the mini-project, the bring-up note will pull from this directly.
