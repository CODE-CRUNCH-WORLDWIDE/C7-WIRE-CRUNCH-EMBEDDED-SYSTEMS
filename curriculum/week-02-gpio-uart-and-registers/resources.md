# Week 2 — Resources

Every reference here is **free** and **publicly accessible**. Page numbers cite the **September 2024 revision** of each document unless noted; later revisions move tables but not concepts. If a register address moves between revisions, re-check.

## Primary datasheets and manuals

- **RP2040 Datasheet** (Raspberry Pi Ltd, ~640 pages, Sep-2024 rev) — the silicon. This week you will live in three sections:
  - §2.3 (SIO), pp. 39–70 — the single-cycle I/O block, the source of `SIO_GPIO_OUT`, `SIO_GPIO_OUT_SET`, `SIO_GPIO_OUT_CLR`, `SIO_GPIO_OUT_XOR` at `SIO_BASE = 0xd000_0000` (table on p. 41).
  - §2.19 (IO_BANK0 + PADS_BANK0), pp. 240–280 — the pin mux, the pad control, the per-pin `GPIOn_CTRL` register (offset `0x004 + n*0x008` from `IO_BANK0_BASE = 0x4001_4000`). Function-select table on p. 244.
  - §4.2 (UART), pp. 425–462 — the PL011 PrimeCell, the integer + fractional baud-rate divisor (`UARTIBRD`, `UARTFBRD`), the 32-byte FIFOs, the `UARTDR` data register at offset `0x000` from `UART0_BASE = 0x4003_4000`.
  <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>
- **Raspberry Pi Pico W Datasheet** (~30 pages) — the *board*, not the chip. Pinout on p. 4; UART0 default pins GP0 (TX) / GP1 (RX) on p. 5.
  <https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf>
- **ARM Cortex-M0+ Devices Generic User Guide** (ARM DUI 0662B, ~110 pages) — the core. This week you need only §2.3 (Memory model — peripheral region, device-type memory, ordering) and §4.1 (NVIC — interrupt configuration), pp. 2-12 and 4-2 respectively.
  <https://developer.arm.com/documentation/dui0662/b/>
- **ARMv6-M Architecture Reference Manual** (DDI 0419, ~440 pages) — the instruction set. This week you will look up exactly one instruction: `STR` (store register), §A6.7.124, p. A6-156. The 16-bit Thumb encoding is what your `*(volatile uint32_t *)0xd000_001c = …` compiles to.
  <https://developer.arm.com/documentation/ddi0419/latest/>
- **ARM PrimeCell UART (PL011) Technical Reference Manual** (ARM DDI 0183, ~84 pages) — the IP block the RP2040 UART block is. The RP2040 datasheet §4.2 cites this document directly. Read it if a register field on the RP2040 confuses you; the PL011 TRM is the authoritative source for the divisor math.
  <https://developer.arm.com/documentation/ddi0183/g/>

## Pi Pico SDK reference

- **Raspberry Pi Pico C/C++ SDK Documentation** (~250 pages) — every public function with a one-paragraph example. This week you will read the GPIO and UART sections cover to cover.
  <https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf>
- **`pico-sdk` source on GitHub** — clone this. Lecture 3 will walk `src/rp2_common/hardware_gpio/gpio.c` line by line.
  <https://github.com/raspberrypi/pico-sdk>
- **`pico-examples` on GitHub** — `uart/`, `gpio/`, and `pio/uart_tx/` are the three directories you want this week.
  <https://github.com/raspberrypi/pico-examples>

## Logic analyzer and scope references

- **Saleae Logic 2 user guide, "Async Serial" analyzer** — the protocol decoder you will use for UART work this week. Sample rate ≥ 5x baud is recommended; at 115200 baud that means ≥ 600 kHz sample rate. The Saleae default 4 MHz is fine.
  <https://support.saleae.com/protocol-analyzers/async-serial>
- **`sigrok` / PulseView "uart" decoder** — the open-source equivalent. Sample rate config and `pulseview --help` document the CLI.
  <https://sigrok.org/wiki/Protocol_decoder:Uart>

## Free books and write-ups

- **"Raspberry Pi Pico — Programming the RP2040 in C"** (informit.com, chapter excerpts free) — Harry Fairhead's chapter on direct register access. The register-level perspective.
- **"Bare-Metal Programming for ARM"** (Daniels Umanovskis, free PDF) — different chip, same mental model. Read the chapter on memory-mapped I/O.
  <https://github.com/umanovskis/baremetal-arm>
- **"PL011 UART deep-dive"** (Adam Greenfield, free blog post series) — covers the divisor math and the 16x oversampling story in more depth than the ARM TRM.

## Videos (free)

- **"Modern Embedded Systems Programming Course" — Quantum Leaps / Miro Samek**, lessons on memory-mapped I/O and direct register access. STM32 target, but the discipline transfers exactly.
- **"Raspberry Pi Pico Programming with C/C++ SDK" — Shawn Hymel / DigiKey**: search YouTube. Episode on GPIO and UART is ~25 min.

## Tools you will use this week

| Command | What it does |
|---------|--------------|
| `arm-none-eabi-objdump -d build/blink.elf` | Disassemble the firmware; find your `STR` to `0xd000_001c` |
| `arm-none-eabi-nm -S build/blink.elf` | List symbols with sizes; confirm `gpio_put` is the size you expect |
| `arm-none-eabi-readelf -S build/blink.elf` | Section headers — confirm `.text` lives in flash, `.data`/`.bss` in SRAM |
| `picotool info -a` | Inspect a Pi Pico in BOOTSEL mode (USB descriptor) |
| `screen /dev/tty.usbserial-* 115200` | Open a serial terminal; `Ctrl-A k` to quit |
| `picocom -b 115200 /dev/ttyUSB0` | Same, with line-editing; `Ctrl-A Ctrl-X` to quit |
| `mpremote connect /dev/ttyACM0 repl` | MicroPython REPL — useful for "send one byte right now" experiments |
| `pulseview` | Open-source logic analyzer GUI; loads `.sr` files |
| `saleae-logic` / Saleae Logic 2 | Commercial analyzer GUI; loads `.sal` files |

## Datasheet pages cheat-sheet (Sep-2024 rev)

You will refer to these pages often this week. Mark them in your local PDF.

| Topic | Section | Pages |
|-------|--------:|------:|
| Memory map (peripheral base addresses) | §2.2 | 23–24 |
| SIO overview | §2.3.1 | 39–41 |
| SIO register table | §2.3.1.7 | 41–46 |
| `SIO_GPIO_OUT` / `_SET` / `_CLR` / `_XOR` | §2.3.1.7 | 42–43 |
| `SIO_GPIO_OE` / `_SET` / `_CLR` / `_XOR` | §2.3.1.7 | 43–44 |
| IO_BANK0 overview | §2.19 | 240–245 |
| GPIO function-select table | §2.19.6.1 | 244 |
| `GPIOn_CTRL` register | §2.19.6.1 | 247 |
| PADS_BANK0 overview | §2.19.4 | 290–294 |
| `PADS_BANK0_GPIOn` register (pull, drive, slew) | §2.19.4 | 294 |
| UART overview | §4.2.1 | 425–428 |
| UART register table | §4.2.7 | 431 |
| `UARTDR` (data register) | §4.2.7.1 | 432 |
| `UARTFR` (flag register) | §4.2.7.3 | 434 |
| `UARTIBRD` (integer baud-rate divisor) | §4.2.7.4 | 435 |
| `UARTFBRD` (fractional baud-rate divisor) | §4.2.7.5 | 435 |
| `UARTLCR_H` (line control) | §4.2.7.6 | 436 |
| `UARTCR` (control register) | §4.2.7.7 | 437 |
| Baud-rate divisor formula | §4.2.7.1 | 432 |
| UART electrical characteristics | §5.4 | 627 |

## Glossary

| Term | One-line definition |
|------|---------------------|
| **SIO** | Single-cycle I/O — the RP2040's CPU-local register bank for fast GPIO read/write, at `0xd000_0000` |
| **IO_BANK0** | The pin-mux peripheral — selects function (UART/SPI/I2C/PWM/SIO/PIO) per pin; `0x4001_4000` |
| **PADS_BANK0** | The pad-control peripheral — pull, drive strength, slew, schmitt; `0x4001_c000` |
| **PL011** | ARM PrimeCell UART, the IP block on the RP2040 UART0/UART1; the RP2040 datasheet cites the ARM PL011 TRM |
| **UARTDR** | UART Data Register — write to TX, read to RX, at `UARTn_BASE + 0x000` |
| **UARTIBRD / UARTFBRD** | Integer / fractional baud-rate divisor — divisor = `clk_peri` / (16 × baud) |
| **8N1** | 8 data bits, no parity, 1 stop bit — the universal UART default |
| **Bit period** | 1 / baud rate. At 115200 baud: 8.681 µs. At 921600 baud: 1.085 µs. |
| **Atomic alias** | The `+0x1000` / `+0x2000` / `+0x3000` offsets that perform XOR / SET / CLR in hardware; uniform across all RP2040 peripherals |
| **Debounce** | Filtering the mechanical bounce of a switch contact; typically a 20 ms low-pass timer in software |
