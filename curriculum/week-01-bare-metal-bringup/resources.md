# Week 1 — Resources

Every reference here is **free** and **publicly accessible**. Page numbers cite the September 2024 revision of each document unless noted; later revisions move tables but not concepts.

## Primary datasheets and manuals

- **RP2040 Datasheet** (Raspberry Pi Ltd, ~640 pages) — the silicon. Sections you will hit this week: §1 (Introduction), §2 (System Description), §2.19 (GPIO), §4.7 (UART). Memory-map summary on p. 24; GPIO function table on p. 244.
  <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>
- **Raspberry Pi Pico W Datasheet** (~30 pages) — the *board*, not the chip. Pinout on p. 4; the CYW43439 wireless module on p. 14; on-board LED wired to `WL_GPIO0` (not GP25 like the original Pico) on p. 5.
  <https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf>
- **Pi Pico C/C++ SDK Documentation** (~250 pages) — every function in `pico-sdk` with one-paragraph examples.
  <https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf>
- **Getting Started with Raspberry Pi Pico-Series** (~70 pages) — the official "first build" guide. Read appendices A (Linux), B (macOS), C (Windows) for your platform.
  <https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf>
- **ARM Cortex-M0+ Devices Generic User Guide** (ARM DUI 0662B, ~110 pages) — the core, not the chip. NVIC, SysTick, exception model.
  <https://developer.arm.com/documentation/dui0662/b/>
- **ARMv6-M Architecture Reference Manual** (DDI 0419, ~440 pages) — the instruction set. Free with an ARM developer account.
  <https://developer.arm.com/documentation/ddi0419/latest/>

## Toolchain documentation

- **GNU Arm Embedded Toolchain (release 13.2.Rel1)** — `arm-none-eabi-gcc` and friends:
  <https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads>
- **OpenOCD User's Guide** — the open-source debug server:
  <https://openocd.org/doc/html/index.html>
- **`probe-rs` book** — the modern Rust-flavored alternative to OpenOCD (works with C firmware too):
  <https://probe.rs/docs/>
- **GDB documentation, chapter on embedded targets**:
  <https://sourceware.org/gdb/current/onlinedocs/gdb.html/Remote-Debugging.html>
- **CMake reference for embedded projects** — `target_link_libraries` and toolchain files:
  <https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html>

## Pi Pico W specific

- **Pico-SDK on GitHub** — clone this. You will read its source repeatedly.
  <https://github.com/raspberrypi/pico-sdk>
- **`pico-examples` on GitHub** — the canonical example library. `blink/`, `hello_serial/`, and `hello_world/uart/` are the three you want this week.
  <https://github.com/raspberrypi/pico-examples>
- **`picoprobe` / `debugprobe`** — turn a second Pico into a CMSIS-DAP SWD probe for ~$4 instead of buying a $20 J-Link:
  <https://github.com/raspberrypi/debugprobe>

## MicroPython

- **MicroPython for RP2040 firmware downloads** — grab the latest stable `.uf2`:
  <https://micropython.org/download/RPI_PICO_W/>
- **MicroPython documentation, RP2 port**:
  <https://docs.micropython.org/en/latest/rp2/quickref.html>
- **`mpremote` tool** — copy files to the board over the REPL without a hand-typed REPL session:
  <https://docs.micropython.org/en/latest/reference/mpremote.html>

## Hardware procurement (Pi Pico W)

| Vendor | Price | Notes |
|--------|------:|-------|
| Raspberry Pi Direct (sparkfun.com, adafruit.com, pi-shop.us) | ~$7 | Authorised resellers; usually in stock |
| Pimoroni | £6.30 (~$8) | UK / EU shipping fastest |
| The Pi Hut | £6.50 | Stocks the headers-pre-soldered variant for £8 |
| Aliexpress / Amazon third-party | $4–$15 | Clone risk: verify the CYW43439 module is present, not a "Pico" sold as "Pico W" |

The Pico W (with the CYW43439 wireless module) is required. The Pico (no W) does **not** have the LED wired to a normal GPIO; on it the LED is GP25, on Pico W the LED is `WL_GPIO0` behind the wireless chip. Your code differs on one line.

Other parts you want on the bench this week (~$15 total): a USB-A to micro-USB cable rated for data (not power-only), a half-size breadboard, a CP2102 or CH340 USB-serial dongle, one tactile push-button, one 10 kΩ pull-up resistor, one 220 Ω LED current-limit (optional — the on-board LED is enough for blink). A $12 Saleae-clone 8-channel 24 MHz USB logic analyzer (works with `sigrok`/PulseView) helps but is not required for Week 1.

## Free books and write-ups

- **"Getting Started With Raspberry Pi Pico"** (HackSpace Magazine, free PDF, ~140 pages) — friendlier than the official guide for the first hour.
  <https://hackspace.raspberrypi.com/books/picobook>
- **"Bare Metal STM32 Programming"** (Lee Lup Yuen, free article series) — different chip, same mental model. Read for the linker-script intuition we will need in Week 3.
  <https://lupyuen.github.io/articles/stm32>
- **"Embedded Rust on the Pi Pico"** (rp-rs project documentation, free):
  <https://github.com/rp-rs/rp-hal>

## Videos (free)

- **"Bare-Metal Embedded Programming with C and STM32" — Quantum Leaps / Miro Samek**: search YouTube; the "Modern Embedded Systems Programming Course" series. Different chip, same first-principles approach.
- **"Raspberry Pi Pico C SDK Getting Started" — Raspberry Pi Foundation**: <https://www.youtube.com/@RaspberryPi>

## Tools you will use this week

| Command | What it does |
|---------|--------------|
| `arm-none-eabi-gcc --version` | Confirm the cross-compiler is installed |
| `arm-none-eabi-size build/blink.elf` | Print `.text` / `.data` / `.bss` sizes in bytes |
| `arm-none-eabi-objdump -d build/blink.elf` | Disassemble the firmware image |
| `openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg` | Start an OpenOCD GDB server |
| `probe-rs run --chip RP2040 build/blink.elf` | Flash + run + `defmt` log in one command |
| `picotool info -a` | Inspect a Pi Pico in BOOTSEL mode (USB) |
| `mpremote connect /dev/ttyACM0 repl` | Drop into the MicroPython REPL |

## Glossary

| Term | One-line definition |
|------|---------------------|
| **MCU** | Microcontroller: CPU + RAM + flash + peripherals on one die, no MMU |
| **MPU** | Microprocessor: CPU only; needs external RAM, external storage, often an MMU |
| **SoC** | System-on-Chip: a superset, often an MPU plus radio / GPU / display controller |
| **SWD** | Serial Wire Debug — ARM's 2-pin debug protocol (SWCLK, SWDIO) |
| **UF2** | Microsoft's USB Flashing Format — the drag-and-drop image format the Pico bootrom accepts |
| **HAL** | Hardware Abstraction Layer — the vendor's C library; we will outgrow this by Week 3 |
| **SDK** | Software Development Kit — `pico-sdk` here; bundles HAL + build system + examples |
| **CMSIS-DAP** | The ARM-standard USB protocol every modern debug probe speaks |
| **PIO** | Programmable I/O — the RP2040's two state-machine blocks; we revisit in Week 7 |
| **VOH / VOL** | Output-high / output-low voltage at a pin; in the datasheet, always with a current spec |
