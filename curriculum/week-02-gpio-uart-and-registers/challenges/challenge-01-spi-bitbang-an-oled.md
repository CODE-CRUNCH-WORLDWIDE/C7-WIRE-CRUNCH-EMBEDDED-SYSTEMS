# Challenge 1 — Bit-Bang SPI to Drive an SSD1306 OLED

**Time estimate:** ~180 minutes (most of it the first time the OLED shows the wrong pixel pattern; ~30 min of coding).

## Problem statement

Wire a 128×32-pixel **SSD1306 OLED** (the cheap I2C+SPI module, set to SPI mode via the on-PCB solder jumper) to four GPIOs on the Pi Pico W. Drive it by **bit-banging** the SPI clock and MOSI signals — that is, toggling GPIOs in software with no use of the RP2040's hardware SPI block. Initialize the OLED, then display the text `crunch-wire w02 boot` in a 5×7 font.

You will measure the actual SPI clock rate you achieve, compare it to the SSD1306's 10 MHz maximum, and document why the **hardware SPI block** is the right answer in production. This challenge exists to make the next eight peripherals (SPI, I2C, CAN, USB, ADC, DAC, PWM, DMA) easier — once you have bit-banged a clock, you understand what every silicon peripheral abstracts away.

## Acceptance criteria

- [ ] A new directory `c7-week02-spi-bitbang-oled/` containing `oled.c`, `font5x7.h`, `CMakeLists.txt`, and `pico_sdk_import.cmake`.
- [ ] The SSD1306 OLED is wired to the Pi Pico W with:
  - **GP10** → OLED SCK (clock, output, push-pull)
  - **GP11** → OLED MOSI / SDA (data, output, push-pull)
  - **GP12** → OLED CS (chip-select, active-low, output)
  - **GP13** → OLED DC (data/command, output)
  - **GP14** → OLED RES (reset, active-low, output)
  - **3V3(OUT)** → OLED VCC
  - **GND** → OLED GND
- [ ] The firmware uses **no** `hardware_spi.h` calls. All clock and data transitions are direct GPIO writes (either `gpio_put` or raw `SIO_GPIO_OUT_SET/CLR`).
- [ ] On boot, the OLED displays `crunch-wire w02 boot` on the first line (top of the 32-pixel display) within 3 seconds of power-up.
- [ ] You capture a Saleae or PulseView trace of SCK and MOSI during one byte of init data, decoded with the analyzer's **SPI** decoder (mode 0, 8 bits, MSB-first). The bytes match the SSD1306 init sequence in the datasheet (see hints).
- [ ] You measure the actual bit-banged SPI clock rate. Expected: **0.5–2 MHz** depending on how tight the loop is. Document your number.
- [ ] A `notes/spi-bitbang-oled.md` documents the wiring, the SSD1306 init sequence with citations, the measured clock rate, and a one-paragraph answer to "why is the hardware SPI block better here?"

## Hints

<details>
<summary>SPI protocol — minimum viable summary</summary>

SPI (Serial Peripheral Interface) is a 4-wire synchronous bus:

- **SCK** — clock, driven by the master (us). Bits are sampled on one edge and shifted on the other.
- **MOSI** — Master Out / Slave In. Data from us to the OLED.
- **MISO** — Master In / Slave Out. Data from the OLED to us. **The SSD1306 in display mode does not use MISO** — it is write-only. So we don't wire it.
- **CS** — Chip Select, active-low. The OLED only listens when CS is low.

There are four SPI "modes" defined by:
- **CPOL** (clock polarity): 0 = idle low, 1 = idle high.
- **CPHA** (clock phase): 0 = sample on first edge, 1 = sample on second.

The SSD1306 wants **Mode 0**: CPOL=0 (clock idles low), CPHA=0 (sample on rising edge, shift on falling). The standard bit-bang loop is:

```
   for each bit in the byte (MSB first):
     set MOSI = bit
     pulse SCK high
     pulse SCK low
```

The SSD1306 samples MOSI on the rising edge. At ~1 MHz bit-bang rate, your "pulse high" and "pulse low" can each be a single GPIO write with no delay.

</details>

<details>
<summary>SSD1306 init sequence (cite: SSD1306 datasheet, Solomon Systech, Sep 2008, Rev 1.1, p. 64)</summary>

The SSD1306 needs a 25-byte command sequence to come out of reset and start displaying. Each byte is sent with **DC = 0** (command). After init, pixel data is sent with **DC = 1** (data).

```c
static const uint8_t ssd1306_init_cmds[] = {
    0xAE,             /* DISPLAYOFF */
    0xD5, 0x80,       /* SETDISPLAYCLOCKDIV, default ratio */
    0xA8, 0x1F,       /* SETMULTIPLEX, 31 (for 32-pixel-tall display) */
    0xD3, 0x00,       /* SETDISPLAYOFFSET = 0 */
    0x40,             /* SETSTARTLINE = 0 */
    0x8D, 0x14,       /* CHARGEPUMP, enable */
    0x20, 0x00,       /* MEMORYMODE = horizontal */
    0xA1,             /* SEGREMAP, column 127 mapped to SEG0 */
    0xC8,             /* COMSCANDEC */
    0xDA, 0x02,       /* SETCOMPINS, sequential, no remap (for 128×32) */
    0x81, 0x8F,       /* SETCONTRAST, mid-range */
    0xD9, 0xF1,       /* SETPRECHARGE, default */
    0xDB, 0x40,       /* SETVCOMDETECT, default */
    0xA4,             /* DISPLAYALLON_RESUME (use RAM) */
    0xA6,             /* NORMALDISPLAY (1 = pixel on) */
    0x2E,             /* DEACTIVATE_SCROLL */
    0xAF,             /* DISPLAYON */
};
```

The reset sequence (before init): hold RES low for ≥ 3 µs, then release it high; wait 100 ms; then send the init bytes. Skipping the reset works ~60% of the time; do it.

</details>

<details>
<summary>Bit-bang send-byte primitive</summary>

```c
#include "pico/stdlib.h"

#define PIN_SCK   10
#define PIN_MOSI  11
#define PIN_CS    12
#define PIN_DC    13
#define PIN_RES   14

static inline void spi_bitbang_byte(uint8_t b) {
    for (int i = 7; i >= 0; --i) {
        gpio_put(PIN_MOSI, (b >> i) & 1u);
        gpio_put(PIN_SCK, 1);
        /* No delay — the SDK gpio_put already takes ~10 cycles, well over
         * the SSD1306's 100 ns minimum clock-high pulse. */
        gpio_put(PIN_SCK, 0);
    }
}

static void ssd1306_command(uint8_t cmd) {
    gpio_put(PIN_DC, 0);   /* command mode */
    gpio_put(PIN_CS, 0);
    spi_bitbang_byte(cmd);
    gpio_put(PIN_CS, 1);
}

static void ssd1306_data(uint8_t data) {
    gpio_put(PIN_DC, 1);   /* data mode */
    gpio_put(PIN_CS, 0);
    spi_bitbang_byte(data);
    gpio_put(PIN_CS, 1);
}
```

For the **fast** version, replace `gpio_put(PIN_SCK, 1)` with `SIO_GPIO_OUT_SET = 1u << PIN_SCK` and `gpio_put(PIN_SCK, 0)` with `SIO_GPIO_OUT_CLR = 1u << PIN_SCK`. This drops you to ~2 cycles per write and pushes the bit-bang clock from ~500 kHz to ~2 MHz.

</details>

<details>
<summary>The font</summary>

A 5×7 ASCII font is the smallest readable font on a 32-pixel-tall display. Use one of:

- The `ssd1306_font.h` in `pico-examples/i2c/oled_i2c/` — public domain, 5×7.
- The "Adafruit Standard 5x7 Font" — free; copy `glcdfont.c` from the Adafruit_GFX library.
- Write your own. 96 glyphs (printable ASCII) × 5 bytes per glyph = 480 bytes of `.text`.

Each glyph is **5 columns × 7 rows**, encoded as 5 bytes, each byte a column. Bit 0 = top pixel, bit 6 = bottom. The 8th bit (bit 7) is unused. To draw "C", you'd send the bytes for the 'C' glyph followed by one blank-column byte (the inter-character gap).

</details>

<details>
<summary>The page-addressing model — why "y" is in groups of 8</summary>

The SSD1306 stores pixels in **pages** of 8 rows. The 128×32 display has 4 pages (page 0 = rows 0–7, page 1 = rows 8–15, etc.). To write a 5×7 glyph at the top of the screen, you send 5 bytes to page 0; the bottom-row pixel (bit 7 of the byte) is left blank.

To position the cursor, send three commands before the data:

```c
ssd1306_command(0x21);          /* SET_COLUMN_ADDRESS */
ssd1306_command(col_start);     /* 0 to 127 */
ssd1306_command(col_end);       /* col_start + glyph_width - 1 */
ssd1306_command(0x22);          /* SET_PAGE_ADDRESS */
ssd1306_command(page_start);    /* 0 to 3 */
ssd1306_command(page_end);      /* same as start for one row */
```

Then send 5 data bytes per glyph. For "crunch-wire w02 boot" (~20 characters at 6 px each = 120 px wide), this fits inside the 128 px width with 8 px of margin.

</details>

<details>
<summary>Measuring the bit-bang clock rate</summary>

Capture SCK on a Saleae at 24 MHz sample rate. During one byte transfer (8 SCK pulses), measure the time between the first rising edge and the eighth rising edge. Divide by 7 to get the clock period; invert to get the rate.

Expected:
- SDK `gpio_put` path: ~500 kHz – 1 MHz (bit period ~1 µs).
- Raw `SIO_GPIO_OUT_SET/CLR` path: ~1.5 MHz – 2.5 MHz (bit period ~0.4 µs).
- Hardware SPI block at the default 1 MHz: a clean 1.000 MHz square wave with consistent edges.

The bit-banged versions will show **timing jitter** — some clock pulses are 1.0 µs, some are 1.2 µs, depending on what the compiler and the loop look like. The hardware SPI block does not jitter. This is the load-bearing observation of the challenge.

</details>

## What to capture

In `notes/spi-bitbang-oled.md`:

```
# Challenge 1 — Bit-Bang SPI to SSD1306

## Wiring

[Photo or ASCII diagram, 5 signals + power]
SCK: GP10, MOSI: GP11, CS: GP12, DC: GP13, RES: GP14, VCC: 3V3(OUT), GND: GND.
SSD1306 module solder jumper set to SPI mode (cite the module's silk).

## Init sequence

[25 bytes from the table above, with the data sheet citation]

## Bit-bang loop (SDK gpio_put version)

[Code snippet]

## Measured clock rate

  SDK version:        720 kHz (cursor measurement, 7 SCK periods = 9.7 µs)
  Raw register version: 2.1 MHz (7 SCK periods = 3.3 µs)
  Hardware SPI default: 1.000 MHz (would be, if we used it)

## Saleae trace (SCK + MOSI during one init byte)

[Screenshot with SPI decoder applied; decoded byte matches init table]

## Reflection — why the hardware SPI block is the right answer

  1. Jitter. The bit-banged clock varies by ~200 ns per pulse, depending on
     what the CPU was doing. A hardware SPI block produces a clock with
     <10 ns jitter at 1 MHz.
  2. CPU load. The bit-bang loop pins the CPU at 100% for the duration of
     the transfer. At 1 KB of pixel data and ~700 kHz, that is ~12 ms of
     CPU. Hardware SPI with DMA hands the transfer off and lets the CPU
     do something useful.
  3. Determinism. Hardware SPI honors the SSD1306's setup/hold times
     exactly. The bit-bang version "usually works" but will glitch on a
     compiler upgrade or a clock-config change.

  In production, you use hardware SPI plus DMA. The bit-bang version is
  for understanding what's underneath, and for the rare case where you
  have one MOSI pin too few and need to slow-bit-bang a config byte to a
  one-off chip.
```

## Stretch goals

- Replace the bit-bang loop with the hardware SPI block (`spi_init(spi1, 1_000_000); spi_set_format(...); spi_write_blocking(...)`). Measure: how fast does the same `crunch-wire w02 boot` display appear? How does the binary size change?
- Scroll the text horizontally by 1 pixel every 100 ms. This forces you to redraw the whole row from a software back-buffer; you will discover why most OLED libraries maintain a 512-byte back-buffer and re-send all of it on each frame.
- Display a 32×32 bitmap (a small Crunch Wire logo, say) instead of text. You will need to write the page-addressing for a 4-page-tall, 32-column-wide block.
- Wire a second OLED on the same SCK/MOSI/DC lines but a different CS line. Display two different lines of text. This proves SPI's multi-slave story: same bus, separate CS, no extra clock.
- Port the same firmware to the **hardware SPI block** and use **DMA** to push the framebuffer. Measure CPU load (toggle a free GPIO inside the loop; capture on Saleae; measure duty cycle). You should see CPU load drop from ~100% during transfer to ~1%. This is a preview of Week 7.

## Why this matters

Every serial bus on a microcontroller — SPI, I2C, UART, CAN, even USB at the lowest level — is, underneath, a clock line and some data lines being toggled with carefully managed timing. The bit-bang version of SPI exposes the timing problem in its rawest form: you write the toggle, you measure the jitter, you see why the hardware block exists.

A C7 graduate who can bit-bang an SPI bus from first principles can, on demand, bring up any silicon SPI peripheral in any chip family in under an hour, because they understand what the hardware is abstracting. A C7 graduate who has only ever called `spi_write_blocking()` is at the mercy of every vendor SDK they ever touch.

Spend the three hours. By Week 8, you will be grateful.
