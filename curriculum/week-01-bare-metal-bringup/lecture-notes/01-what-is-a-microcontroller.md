# Lecture 1 — What Is a Microcontroller?

> **Outcome:** You can name three parts of an MCU, distinguish it from a CPU and an SoC with concrete examples, sketch the RP2040 memory map from memory, and explain why "embedded" is not "Linux with fewer features."

---

## 1. The taxonomy, in numbers not adjectives

The words *CPU*, *microprocessor*, *microcontroller*, and *system-on-chip* get used interchangeably by people who have never opened a datasheet. The line between them is a function of three measurable axes:

1. **Memory integration.** Is RAM and non-volatile storage on the die, or off-die behind a memory controller?
2. **Peripheral integration.** Are the UART, SPI, ADC, and timers part of the same silicon, or are they external chips behind a bus?
3. **OS expectation.** Does the part assume a fully-featured operating system (MMU, virtual memory, supervisor mode), or does it run a single statically-linked binary directly out of flash?

Pin those axes against the parts a C7 student will actually touch:

| Part | Family | On-die RAM | On-die flash | Peripherals on-die | MMU | Typical OS |
|---|---|---:|---:|---|---|---|
| **RP2040** (Pi Pico W) | MCU, Cortex-M0+ ×2 | 264 KB | 0 (external 2 MB QSPI) | UART, SPI, I2C, PWM, ADC, USB, PIO ×2 | no | bare-metal / FreeRTOS / Zephyr / MicroPython |
| **STM32F446RE** (Nucleo-F446) | MCU, Cortex-M4F | 128 KB | 512 KB | as above + CAN, SDIO, DAC, DCMI | no | bare-metal / FreeRTOS / Zephyr |
| **ESP32-S3** (S3-DevKitC) | MCU/SoC, Xtensa LX7 ×2 | 512 KB | 0 (external 8 MB QSPI) | as above + Wi-Fi, BLE 5 | no | FreeRTOS (ESP-IDF), Zephyr |
| **ATmega328P** (Arduino UNO) | MCU, AVR 8-bit | 2 KB | 32 KB | UART, SPI, I2C, ADC, timers | no | bare-metal |
| **Broadcom BCM2711** (Pi 4) | SoC / MPU, Cortex-A72 ×4 | 0 (external 4–8 GB DDR4) | 0 (external SD/USB) | GPU, HDMI, USB 3 PHY | yes | Linux |
| **Intel Core i7-1280P** | CPU | 0 (external DDR5) | 0 (external NVMe) | none meaningful | yes | macOS / Linux / Windows |
| **Apple M3 Max** | SoC | unified DRAM (off-die-on-package) | external NVMe | GPU, Neural Engine, ISP, video codecs | yes | macOS |

Read across the rows. The same word — "processor" — covers a part with 2 KB of total RAM and a part with 128 GB of unified memory. That is why we never say "processor" without qualifying it.

### The three definitions, with units

- A **CPU** is the compute core alone. An Intel i7 die is mostly CPU plus cache. It expects external memory (DDR5), external storage (NVMe over PCIe), and an OS with an MMU. Sold alone on a motherboard, it does nothing.
- A **microprocessor (MPU)** is a CPU plus what a textbook CPU lacks: caches, an MMU, and bus controllers — but still no integrated RAM and no peripherals beyond a memory bus. A Broadcom BCM2837 on a Pi 3 is an MPU. Linux is the assumption.
- A **microcontroller (MCU)** is a CPU plus on-die RAM, on-die or directly-attached flash, and a tray of peripherals (UART, SPI, I2C, ADC, timers, PWM, USB). It runs one program at a time, from reset, with no OS underneath unless you put one there. The RP2040 is an MCU.
- A **system-on-chip (SoC)** is whatever superset somebody is willing to put on one die for a given product. An ESP32-S3 is an MCU + Wi-Fi + BLE — sold as an SoC because the radio is on-die. An Apple M3 is an MPU + GPU + Neural Engine + image processor — sold as an SoC because *everything* is on-die.

Two implications you will use this week:

1. **The Pico W is an MCU**, which means there is no Linux kernel between your `gpio_put(PIN, 1)` and the LED. Your code is the operating system.
2. **The Cortex-M0+ in the RP2040 has no MMU**, which means there is no virtual memory, no `fork()`, no shared libraries, no swap, and no "segfault" in the Linux sense. A bad pointer is a HardFault that locks the part until reset.

---

## 2. The RP2040 in one page

The RP2040 datasheet is 640 pages. You do not read it linearly. You read it the way you read a reference book — by section, on demand. But you need the *shape* of the part in your head before you can navigate the datasheet at all. Here is the shape, drawn the way it lives on the bench:

```
   +---------------------- RP2040 die ----------------------+
   |                                                        |
   |  +-- Cortex-M0+ core 0 --+   +-- Cortex-M0+ core 1 --+ |
   |  |     ~133 MHz max      |   |     ~133 MHz max      | |
   |  |     Thumb-2 subset    |   |     Thumb-2 subset    | |
   |  |     no FPU, no DSP    |   |     no FPU, no DSP    | |
   |  +-----------+-----------+   +-----------+-----------+ |
   |              |                           |             |
   |              +------- AHB-Lite bus ------+             |
   |                          |                             |
   |  +-----------+-----------+-----------+-----------+    |
   |  | SRAM      | SRAM      | SRAM      | SRAM      |    |
   |  | bank 0    | bank 1    | bank 2    | bank 3    |    |
   |  | 64 KB     | 64 KB     | 64 KB     | 64 KB     |    |
   |  +-----------+-----------+-----------+-----------+    |
   |  + striped or non-striped, totalling 264 KB    +      |
   |                                                        |
   |  +-- Peripherals --------------------------------+    |
   |  | UART ×2     SPI ×2      I2C ×2      ADC ×1   |    |
   |  | PWM ×8      Timer ×1    USB 1.1     PIO ×2   |    |
   |  | RTC ×1      DMA 12 ch   30 GPIO              |    |
   |  +-----------------------------------------------+    |
   |                                                        |
   |  +-- Boot ROM --+     +-- QSPI flash interface --+    |
   |  |   16 KB      |     |   external W25Q16 etc.   |    |
   |  +--------------+     +---------------------------+    |
   |                                                        |
   +--------------------------------------------------------+
                              |
                              v
                +-- external 2 MB QSPI flash --+   (off-die, on the Pico W board)
                +-------------------------------+
```

Five facts about the RP2040 you will not be able to memorize all in one sitting, but you will use all 24 weeks:

1. **Dual core, but it is not magic.** The two M0+ cores share the same bus. They contend for SRAM access. We will use core 1 in Week 9 for an audio task; until then, core 1 sleeps.
2. **264 KB SRAM is small.** A single 1080p video frame at 24 bpp is ~6 MB; you cannot hold one. The whole MicroPython runtime fits in ~80 KB; that is why we can run it. A small TensorFlow Lite Micro model has to fit in the remaining ~100 KB. Plan accordingly.
3. **The flash is external.** 2 MB sits in a QSPI chip on the Pico W board. The CPU executes directly from it via XIP (eXecute-In-Place) through a cache. This is why your first flash takes ~6 seconds and your first run starts in ~50 ms.
4. **The boot ROM is real ROM.** 16 KB of mask-programmed code at address `0x0000_0000`. You cannot change it. It implements the BOOTSEL USB drag-and-drop, the `pico_bootrom` API, and the initial flash setup. Datasheet §2.8.
5. **The two PIO blocks are the RP2040's signature trick.** Two state-machine engines, each four threads, that bit-bang any protocol you can describe in a tiny instruction set: VGA, DVI, WS2812, Ethernet, even DDS audio. We will not touch them this week. We will worship them in Week 11.

### The memory map (datasheet p. 24)

You need this committed to long-term memory by mid-Week 3. For now, recognize the shape:

```
   0x0000_0000 ── 0x0000_3FFF   ROM        (16 KB, mask-programmed boot ROM)
   0x1000_0000 ── 0x101F_FFFF   XIP_BASE   (external QSPI flash, 2 MB cached)
   0x2000_0000 ── 0x2004_1FFF   SRAM       (264 KB across 4 striped banks)
   0x4000_0000 ── 0x4007_FFFF   APB        (slow peripherals: UART, I2C, SPI, ADC, PWM, RTC)
   0x4006_0000 ── 0x4006_FFFF      IO_BANK0       (GPIO function-select registers)
   0x4002_8000 ── 0x4002_BFFF      SIO            (Single-cycle IO, direct GPIO toggle)
   0x5000_0000 ── 0x5000_FFFF   AHB        (DMA, USB, PIO)
   0xE000_0000 ── 0xE00F_FFFF   PPB        (ARM private peripheral bus: NVIC, SysTick)
```

When you write `*(volatile uint32_t *)0xD000_0010 = 1u << 25;` in C — which we will, in Lecture 3 — that address is real, that bit is real, and the LED on GP25 will turn on. Embedded engineering is the first place in your career where there is no abstraction between what you write and what physically happens. That is the appeal.

---

## 3. The Pi Pico W board, vs the chip

The RP2040 is the silicon. The Pi Pico W is a 21 mm × 51 mm carrier board that adds:

- The 2 MB QSPI flash (Winbond W25Q16JV or compatible).
- A 12 MHz crystal feeding the PLL → 133 MHz system clock.
- A 5-pin SWD header (SWDIO, SWCLK, GND, plus 2 not connected).
- A USB micro-B connector and the BOOTSEL button (held during reset = mass-storage mode).
- An on-board buck-boost regulator (RT6150) that takes 1.8 – 5.5 V in on VSYS and produces a clean 3.3 V at 800 mA on `3V3(OUT)`.
- A **CYW43439** wireless module from Infineon, providing 2.4 GHz Wi-Fi (802.11 b/g/n) and Bluetooth 5.0. This is the *W* in "Pico W."
- The on-board green LED, **wired to `WL_GPIO0` on the CYW43439, not to a normal RP2040 GPIO**. This is the single biggest gotcha in Week 1.

That last bullet matters. On the original Pi Pico, the LED was GP25 and you toggled it with a one-line `gpio_put(25, 1)`. On the Pi Pico W, the LED is behind the wireless chip, and toggling it requires bringing up the CYW43 driver (`pico_cyw43_arch_init()` followed by `cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1)`). That is ~6 KB of additional firmware, and the reason your first blink does not work if you copy-paste a Pico (no-W) tutorial.

The Pi Pico W datasheet shows the pinout on p. 4. The CYW43439 wiring is on p. 14. Both are open PDFs. Read them once before Wednesday's lab.

---

## 4. What "bare-metal" means, and why we start here

**Bare-metal** = no operating system. Your program owns the chip. There is one thread of execution, started by the reset handler, that runs `main()` and never returns. There is no scheduler, no `malloc()` you can rely on, no `printf()` until you write one, and no signal that something is wrong other than the LED that fails to blink and the scope that shows a flat line.

This sounds primitive. It is. It is also exactly what 90% of shipping embedded products run, including most of what is in your kitchen, your car, your watch, and the device you read this on. Linux is a luxury reserved for parts with at least ~50 MB of RAM and ~50 MB of storage; the cheap MCUs that ship by the billion every year do not have either.

The right way to start C7 is bare-metal, for three reasons:

1. **Every later abstraction leaks.** FreeRTOS bugs become register bugs; Zephyr bugs become device-tree bugs; MQTT-over-TLS bugs become TLS-state-machine bugs that you trace back to a UART overflow that you trace back to a DMA misconfiguration. If you do not have bare-metal intuition, you cannot debug any of those. We make sure you do, before adding anything.
2. **The toolchain matters.** "I built it" should not be a mystery. The compiler flags, the linker script, the OpenOCD invocation — all visible, all editable, all yours. Week 3 will have you writing a linker script from scratch. This week we are kind: we use the official SDK so the CMake hides the linker script for now. We will earn the right to throw the SDK away.
3. **Determinism.** A bare-metal blink jitters by exactly the time it takes the CPU to execute one branch and one store: ~30 ns at 133 MHz. A MicroPython blink jitters by the GC pause, which on the RP2040 is ~5 ms peak. For an LED, neither matters. For a stepper motor at 200 kHz, the difference is everything. C7 is the course where you learn to *care* about that 5 ms.

---

## 5. The C-vs-MicroPython rule we will keep using

You will do this week's blink twice: once in C, once in MicroPython. This is not "pick a favorite." It is a deliberate calibration exercise. The rule we will defend for 24 weeks:

> **Write firmware in C (or C++ or Rust). Use Python (MicroPython) for prototyping speed.**

Concretely:

- The shipped firmware on the Week-24 capstone is in C and Rust. There is no MicroPython on the bench in Week 24.
- The bench tooling — the script that pokes a register over UART to confirm a clock is up, the script that bulk-flashes a tray of 20 boards in CI, the script that captures and decodes one frame — is fine to write in MicroPython, or in host-side Python over `pyserial`. Often better.

The decision criteria, in writing, with numbers:

| Concern | C / C++ / Rust | MicroPython |
|---|---|---|
| Binary size | ~4–40 KB for a useful program | ~ 80 KB runtime + your code |
| Boot time, reset to `main()` | ~50 ms | ~600 ms |
| Worst-case interrupt latency | < 1 µs achievable | > 1 ms (GC pauses) |
| Time-to-prototype | hours to days | minutes |
| Certifiable (MISRA-C, IEC 61508, FDA) | yes | no |
| Memory footprint for the language runtime | 0 | ~ 60 KB peak |
| OTA-deployable as one signed image | yes (standard) | yes, but the runtime complicates signing |
| Field debug ergonomics | GDB, SWO, ITM | REPL over UART |

When in doubt: prototype the algorithm in MicroPython on the bench. Once the algorithm is right, port the inner loop to C, profile it, and ship the C. This is the workflow Week 6 will formalize for `BME280` over I2C. This week, we just do the same blink twice and read the size output of `arm-none-eabi-size` against the size of the `.uf2` MicroPython produces.

---

## 6. Self-check

Without re-reading:

1. The RP2040 has how much on-die SRAM? On-die flash? Where does the user code live at run-time?
2. Give one concrete part number for each of: CPU, MCU, SoC. Explain in one sentence why each fits the category.
3. The Pi Pico W on-board LED is wired to which signal? Why does this matter for your blink code?
4. Sketch the RP2040 memory map from memory. Where does SRAM start? Where do the GPIO function-select registers live?
5. Name two reasons we start C7 bare-metal instead of with an RTOS.
6. State the C-vs-MicroPython rule of thumb. Give one concrete example where each is the right answer.

If you cannot answer (1) or (4) cleanly, re-read this lecture before Wednesday. We will use both facts in the toolchain lecture.

---

## Further reading

- **RP2040 datasheet** §1 (Introduction, pp. 9–18) and §2.4 (Memory, pp. 24–26).
- **Pi Pico W datasheet** pp. 4 (pinout) and pp. 13–14 (CYW43439).
- **"What Every Programmer Should Know About Memory"** (Ulrich Drepper, 2007). Long, free PDF. Read §2 for the cache primer; the rest is bonus.
- **Hackaday "Pi Pico" series** — a friendly, project-shaped intro: <https://hackaday.com/?s=raspberry+pi+pico>

Next: [Lecture 2 — The Toolchain Tour](./02-the-toolchain-tour.md).
