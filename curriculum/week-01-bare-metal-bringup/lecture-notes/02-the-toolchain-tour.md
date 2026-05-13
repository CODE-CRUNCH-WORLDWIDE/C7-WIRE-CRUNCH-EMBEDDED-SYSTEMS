# Lecture 2 — The Toolchain Tour

> **Outcome:** You can name each program in the embedded toolchain, state in one line what it produces and what it consumes, and run each of them by hand against a Pi Pico W. By Friday you should be able to draw the pipeline from `blink.c` to a blinking LED with no boxes labelled "magic."

---

## 1. Why a separate toolchain

The compiler that built the binary you are reading this on — `clang` on macOS, `gcc` on Linux, `cl.exe` on Windows — produced a binary that runs on the *same* CPU architecture as the compiler itself. That is "native" compilation. Almost no embedded work is native. You write code on an x86_64 or aarch64 laptop and run it on a Cortex-M0+ (ARMv6-M), an Xtensa LX7, an AVR, or a RISC-V core. That is **cross-compilation**.

A cross-compilation toolchain is, conventionally, a set of binaries with a name prefix that identifies the target. The GCC ARM Embedded toolchain we will use this week is the canonical example. Every program in it begins with `arm-none-eabi-`:

| Program | What it does | What it consumes → produces |
|---|---|---|
| `arm-none-eabi-gcc` | The compiler driver | `.c` → `.o` (or `.elf` end-to-end) |
| `arm-none-eabi-g++` | C++ compiler driver | `.cpp` → `.o` |
| `arm-none-eabi-as` | Assembler | `.s` → `.o` |
| `arm-none-eabi-ld` | Linker | `.o` × N + linker script → `.elf` |
| `arm-none-eabi-objcopy` | Format converter | `.elf` → `.bin` / `.hex` / `.uf2` |
| `arm-none-eabi-objdump` | Disassembler / inspector | `.elf` → human-readable dump |
| `arm-none-eabi-size` | Section size reporter | `.elf` → `text/data/bss` sizes |
| `arm-none-eabi-nm` | Symbol lister | `.elf` → name + address + section |
| `arm-none-eabi-gdb` | The debugger | `.elf` + a debug server → an interactive prompt |
| `arm-none-eabi-readelf` | ELF format reader | `.elf` → headers, sections, relocations |

The triplet `arm-none-eabi` decodes as:

- **arm**: target CPU family.
- **none**: target OS. ("none" = bare-metal, no kernel underneath.)
- **eabi**: target ABI. ("Embedded Application Binary Interface" — ARM's calling convention spec for bare-metal code, separate from the Linux GNU/EABI used by `arm-linux-gnueabihf-gcc`.)

You will see other triplets: `xtensa-esp32s3-elf-gcc` for ESP32-S3, `avr-gcc` for AVR, `riscv32-unknown-elf-gcc` for RISC-V MCUs. The grammar is the same.

A useful first reflex: any time something fails with `cannot find -lc` or `undefined reference to '_exit'`, the toolchain found a header but not the right *library* for a bare-metal target. The fix is usually adding `--specs=nosys.specs` (use the no-syscalls libc) or `--specs=nano.specs` (use the smaller `newlib-nano`). We will use both.

---

## 2. The compile/link pipeline, in five boxes

```
   blink.c   blink.h
        \   /
         \ /
          +--------------+
          | arm-none-     |    -c -mcpu=cortex-m0plus -mthumb -O2
          |   eabi-gcc    |    -ffreestanding -fno-builtin
          | (preprocess + |    -Wall -Wextra
          |  compile)     |
          +--------------+
                 |
                 v
            blink.o      pico_stdlib.o      hardware_gpio.o
                 \             |                 /
                  \            |                /
                   +----------------------------+
                   | arm-none-eabi-ld           |   --script=memmap_default.ld
                   | (link with linker script)  |   -lc -lnosys
                   +----------------------------+
                                |
                                v
                            blink.elf
                                |
                +---------------+---------------+
                |               |               |
                v               v               v
       arm-none-eabi-       arm-none-eabi-   arm-none-eabi-
       objcopy              size             objdump -d
       -O binary            (.text/.data/   (disassembly)
       blink.elf            .bss reporter)
       blink.bin
            +
       elf2uf2 ── blink.uf2 (the file you drag onto the BOOTSEL drive)
```

Two things every embedded engineer learns to look at, twice a day, every day:

1. **`arm-none-eabi-size build/blink.elf`** — tells you how big your firmware is. The output:

   ```
      text    data     bss     dec     hex filename
     24876     332    7820   33028    8104 build/blink.elf
   ```

   Means:
   - `.text` = 24,876 bytes — executable code + read-only data in flash.
   - `.data` = 332 bytes — initialized variables (copied from flash to RAM at startup).
   - `.bss` = 7,820 bytes — zero-initialized variables (zeroed in RAM at startup).
   - `dec` = total in decimal; `hex` = same in hex; that is what fits in the binary image.

   On a Pi Pico W with 264 KB SRAM and 2 MB flash, this is comfortable. On an ATmega328P with 32 KB flash and 2 KB SRAM, the same program would not fit. Read the `size` output every time you build. It is a 30-second discipline that catches 30-minute bugs.

2. **`arm-none-eabi-objdump -d build/blink.elf | less`** — disassembles your binary. The output starts with a section listing and then shows every instruction. The first time you find your `main()` function in the disassembly and recognize the prologue (`push {r7, lr}` etc.), the rest of embedded engineering opens up. Do this on Wednesday.

---

## 3. The flash/debug pipeline, in three protocols

Compilation produced a binary. Now you have to get it onto the chip and, ideally, talk to a running chip to debug it. The Pi Pico W gives you two paths.

### Path A: BOOTSEL + UF2 (the easy path)

Hold the BOOTSEL button on the Pico W, plug the USB cable into your laptop, release BOOTSEL. The Pico's boot ROM enumerates as a USB mass-storage device with the volume label `RPI-RP2`. Drag the `blink.uf2` file onto that drive. The boot ROM verifies the UF2 header, writes the contained image to flash, and reboots into the new firmware.

This works without any extra hardware. It is how we will flash for the first three weeks. Its limits:

- **One-shot.** You cannot single-step, set breakpoints, or read live variables. The chip is either running your code or sitting in BOOTSEL — never both.
- **Manual.** A human presses BOOTSEL and a human drags a file. Fine for one board on a bench, bad for a tray of 20 in CI.

Datasheet reference: RP2040 §2.8.4 ("USB Mass Storage Boot Interface").

### Path B: SWD via OpenOCD or probe-rs (the real path)

The RP2040 exposes a **Serial Wire Debug** interface on three pins: SWCLK, SWDIO, and GND. SWD is ARM's 2-pin debug protocol — a serial replacement for JTAG, supported on every Cortex-M part. A *debug probe* speaks SWD on one side and USB on the other.

Three probes you might have:

- **A second Pi Pico flashed as a `debugprobe`**. ~$4. Picoprobe firmware turns it into a USB-CMSIS-DAP probe. This is what we recommend for the cohort.
- **A J-Link EDU Mini.** ~$20. Closed-source binary, but with a GDB server (`JLinkGDBServer`) that works with OpenOCD or directly with `arm-none-eabi-gdb`.
- **A Black Magic Probe.** ~$70. Open-source, has GDB built in, no GDB server needed.

Once a probe is wired, two open-source tools talk to it:

#### OpenOCD

The classic. Started in 2005, written in C, supports every probe and target you have ever heard of. You launch it as a server:

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg
```

It listens on TCP port `3333` for a GDB client. In another terminal:

```bash
arm-none-eabi-gdb build/blink.elf
(gdb) target remote :3333
(gdb) monitor reset halt
(gdb) load
(gdb) continue
```

OpenOCD's strengths: universal target support, scriptable via Tcl, free. Its weaknesses: the config-file architecture is dated, error messages range from sparse to actively misleading, and "did my probe enumerate" is harder to answer than it should be.

#### probe-rs

The modern alternative. Started in 2019 by the Rust embedded working group, written in Rust, but **works with C and C++ firmware too** — it does not care what compiled the `.elf`. The killer feature is one command:

```bash
probe-rs run --chip RP2040 build/blink.elf
```

That single line: detects the probe, halts the target, flashes the image, resets, runs, and streams `defmt` logs back to your terminal. The same pipeline as OpenOCD + GDB, in five characters and no config file. For a Pi Pico W you will use `probe-rs` 80% of the time in C7 even though we are not yet writing Rust.

Recommendation: install both. Use `probe-rs` for fast iterate-and-blink. Use OpenOCD when you need GDB-level introspection (which is most of Week 12).

---

## 4. The GDB-over-SWD session in 60 seconds

Once an OpenOCD or `probe-rs gdb-server` is running and your firmware is loaded, the GDB session you will use 50 times a week looks like:

```
(gdb) target remote :3333         ← connect to the debug server
(gdb) monitor reset halt          ← OpenOCD-specific reset+halt
(gdb) load                        ← flash the .elf to the target
(gdb) break main                  ← set breakpoint at main()
(gdb) continue                    ← run until the breakpoint
(gdb) info registers              ← dump R0–R15, xPSR, etc.
(gdb) x/16xw 0x20000000           ← examine 16 words of SRAM
(gdb) next                        ← step one C line
(gdb) stepi                       ← step one assembly instruction
(gdb) print my_var                ← read a variable by name
(gdb) layout asm                  ← split the screen, show disassembly
(gdb) Ctrl-C                      ← halt the target mid-run
(gdb) backtrace                   ← what's on the call stack?
```

These twelve commands cover ~95% of bare-metal debugging. Write them on a sticky note for your monitor. By Week 12 they will be reflex.

A useful pre-canned `~/.gdbinit` for this course:

```
set confirm off
set pagination off
set print pretty on
set print array on
set print array-indexes on
define rh
  monitor reset halt
end
define lf
  load
end
define rl
  monitor reset halt
  load
  continue
end
```

The `rl` macro becomes your "rebuild and run" shortcut. Use it.

---

## 5. CMake on `pico-sdk` — the build glue

The official `pico-sdk` is a CMake project. CMake is not a build system in itself; it is a build-system *generator* that produces Makefiles (or Ninja files) for your platform. The minimal `CMakeLists.txt` for a Pi Pico W blink looks like:

```cmake
cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(blink C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(PICO_BOARD pico_w)

pico_sdk_init()

add_executable(blink blink.c)
target_link_libraries(blink
    pico_stdlib
    pico_cyw43_arch_none
)
pico_add_extra_outputs(blink)
```

Walk it line by line, because every line earns its keep:

- **Line 1.** CMake minimum version. The SDK needs ≥ 3.13 for the `pico_add_extra_outputs` macro.
- **Line 2.** Pull in the SDK's CMake helpers. `PICO_SDK_PATH` is the env var pointing at your clone of `raspberrypi/pico-sdk`.
- **Line 4.** Declare the project. The three languages (`C CXX ASM`) tell CMake to enable C, C++, and assembly toolchains — yes, even though we write only C; the SDK contains some `.S` files.
- **Line 6.** Tell the SDK which board variant we are on. `pico_w` selects the Pico W board config, which knows about the CYW43439 wiring. Drop this line and your LED will not blink.
- **Line 8.** `pico_sdk_init()` is the SDK's own CMake macro that sets up the toolchain, the linker script, and the startup files. Inside it is the magic — and we will tear that magic apart in Week 3.
- **Line 10.** Declare the executable target.
- **Lines 11–14.** Link against `pico_stdlib` (the SDK's portable HAL — UART, GPIO, time) and `pico_cyw43_arch_none` (the CYW43 driver in "no extra stack" mode — no Wi-Fi or BLE yet, just LED access).
- **Line 15.** Tell the SDK to generate the `.uf2` and `.hex` files alongside the `.elf`. Without this you get an `.elf` you cannot flash via BOOTSEL.

Build it:

```bash
mkdir build && cd build
cmake -DPICO_BOARD=pico_w ..
make -j$(nproc)
```

Output:

```
build/blink.elf          ← the linked binary (for GDB / probe-rs)
build/blink.uf2          ← the drag-and-drop binary (for BOOTSEL)
build/blink.bin          ← raw bytes (for openocd flash write_bank)
build/blink.hex          ← Intel HEX (legacy programmers)
build/blink.dis          ← (if enabled) the disassembly
```

You now have everything you need to flash. Wednesday's exercise puts this on the board.

---

## 6. A pragmatic install order

Each of these is in the exercise sheet with full commands. The order is:

1. **Host C/C++ toolchain.** macOS: `xcode-select --install`. Ubuntu: `sudo apt install build-essential`. Windows: install MSYS2 or WSL2 (recommended).
2. **`arm-none-eabi-gcc` ≥ 13.2.Rel1.** Direct download from ARM, or `brew install --cask gcc-arm-embedded`, or `apt install gcc-arm-none-eabi`.
3. **CMake ≥ 3.13** and **Ninja**. Almost always already there; if not, `brew install cmake ninja`.
4. **`pico-sdk`.** `git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk` and set `PICO_SDK_PATH`.
5. **`picotool`.** For inspecting boards in BOOTSEL: `brew install picotool`.
6. **OpenOCD ≥ 0.12.0** with RP2040 support. The package on Ubuntu 22.04 is too old; build from source or use Raspberry Pi's fork.
7. **`probe-rs` ≥ 0.24.** `cargo install probe-rs --features cli`. Yes, you need Rust installed for this even if you write zero Rust.
8. **`arm-none-eabi-gdb`.** Bundled with the GCC ARM Embedded download on Linux; on macOS install `gdb-multiarch` via Homebrew or use the `lldb` workaround documented in the exercise.

Total install time on a clean machine: ~45 minutes. The exercise sheet covers the macOS-specific code-signing dance for `gdb` and the WSL2-USB-passthrough dance for the Pi Pico.

---

## 7. Self-check

Without re-reading:

1. What does the triplet `arm-none-eabi` mean, piece by piece?
2. What is the output of `arm-none-eabi-size`, in your own words? Why do we care about the `.bss` column?
3. Sketch the compile/link pipeline. Where does the linker script enter? Where does the `.uf2` get generated?
4. Name two ways to get a `.uf2` onto a Pi Pico W. State one advantage and one disadvantage of each.
5. What is SWD? How many wires? Why is it not USB?
6. What is the one CMake line you would change to switch a build between Pi Pico (no W) and Pi Pico W?

---

## Further reading

- **GCC ARM Embedded release notes** for 13.2.Rel1 — the toolchain changelog matters when something works in tutorials but not on your machine.
- **OpenOCD user manual**, chapter "Debug Adapter Configuration" — the canonical reference for probe config files.
- **`probe-rs` book**, chapter "Getting started" — short and current.

Next: [Lecture 3 — Reading a Datasheet](./03-reading-a-datasheet.md).
