# Lecture 3 — Reading a Datasheet

> **Outcome:** You can open the RP2040 datasheet (640 pages, September 2024 rev) and answer five concrete questions about the GPIO and UART blocks inside 10 minutes, using only the table of contents and the search function. You can name the four phases of datasheet reading, and recognize when you are doing each.

---

## 1. The datasheet is not a manual

A datasheet is *not* a tutorial. It is not even, really, documentation in the sense a software person means the word. It is the *contractual specification* between a silicon vendor and the engineer building a board. Every parameter in it carries an implicit "if you violate this, the chip may or may not work; we have not tested past these limits and we are not responsible for what happens." Datasheets are written by hardware engineers under legal review. They read like it.

This has three consequences for how you read them:

1. **Look for the limits, not the typical case.** A line that says `VOH (typ) = 3.0 V at IOH = 4 mA` is interesting. A line that says `VOH (min) = 2.4 V at IOH = 12 mA` is *binding*. Real designs respect the min; tutorials cite the typ. We will use minima.
2. **Every number has units, and the units matter.** "1 MHz max" and "1 MHz typ at 25°C" describe different chips. The temperature range is in the small print on the cover.
3. **Page numbers are not stable.** The RP2040 datasheet has revised twice in 2024 and once in 2023. A reference to "page 244" assumes a specific revision. C7 cites revisions when it matters. You should too.

A datasheet is a tool you reach for to *answer a specific question fast*. It is not bedtime reading. Treat it like a man page: search for the function, read the section, get out.

---

## 2. The four phases of reading a datasheet

When you open a datasheet for a new part for the first time, do these four passes, in order. Each pass takes 10–30 minutes. Resist the urge to skip ahead.

### Phase 1 — The cover, the block diagram, the pin map (~10 min)

You read four things on the cover page:

- The part number and family. ("RP2040." "STM32F446RET6." "ESP32-S3-WROOM-1.")
- The "Features" bullet list. This is marketing-flavored, but it is also the index card: dual M0+, 264 KB SRAM, 30 GPIO, USB 1.1, 2 PIO blocks.
- The package and pin count. (QFN-56 for the RP2040 bare chip; 40 pins exposed on the Pico W carrier board.)
- The "Operating conditions" table on or near page 1. Supply voltage range, operating temperature, max clock. These are the *contract*.

Then flip to the **system block diagram**, typically pages 5–10. For the RP2040 it is on p. 11 of the Sep-2024 rev. The block diagram answers: what is on the die, and how are the blocks wired? Every wire on that diagram is a bus you can probe in your head. Memorize the shape.

Then flip to the **pin map and pin function table**. For the Pi Pico W carrier board, p. 4 of the board datasheet. Each GPIO has 8 alternate functions — UART, SPI, I2C, PWM, PIO0, PIO1, SIO, NULL. The table tells you which function is on which pin in which AF (alternate-function) slot.

What you should have on a sticky note after Phase 1 of the Pi Pico W:

```
   RP2040, dual Cortex-M0+ @ 133 MHz, 264 KB SRAM, 2 MB ext QSPI flash
   VDD 1.8–3.6 V; T_op -20 °C to +85 °C
   30 GPIO, 8 AF each; UART0 default on GP0(TX)/GP1(RX); LED on WL_GPIO0
```

That's it. Three lines. You will not remember more on Phase 1.

### Phase 2 — The memory map and the register reference (~20 min)

Now you find the section called "Memory Map" or "System Architecture." For the RP2040 it is §2.2 starting on p. 23. Two things you write down:

1. **The base address of every peripheral.** For our GPIO work this week: `IO_BANK0_BASE = 0x4001_4000`, `SIO_BASE = 0xD000_0000`. For UART: `UART0_BASE = 0x4003_4000`. These are the addresses your C pointers will hold.
2. **The page or section number for each peripheral's register reference.** GPIO is §2.19. UART is §4.2. You are not memorizing the registers themselves yet; you are memorizing where to find them. The datasheet is a *random-access* document.

Then pick one peripheral relevant to today's task and read its **register reference table**. For Lab 02 (blink in C via the SDK) you would not actually need this — the SDK hides it — but you will need it for Lab 03 (blink without the SDK, Week 3). Each register reference table looks like:

```
  GPIO_OUT (offset 0x010 from SIO_BASE)
    Direct out, 30 GPIO. Write 1 to set; read returns current output value.

    Bit  Field         Reset  R/W  Description
    [29:0] GPIO_OUT     0      RW   Per-pin direct output.  Bit n controls GPIOn.
    [31:30] reserved    -      -    -
```

Three things in every register table: the **offset** from the peripheral's base address, the **reset value** (what the register holds after a reset, before your code runs), and the **access type** of each field (R = read-only; W = write-only; RW = read-write; WC = write-1-to-clear; etc.). All three matter.

For the Pi Pico W on-board LED via the CYW43, you will not poke `GPIO_OUT` directly this week — the LED is behind the wireless chip. But for any *other* GPIO on the board (say, GP15, where Wednesday's button will live), this register is the one you write.

### Phase 3 — The peripheral *narrative* sections (~20 min)

Around every register reference table is a *narrative*: prose explaining what the peripheral does, how it is clocked, what its modes are, what its DMA channels look like. For the RP2040 GPIO block this is §2.19.1–2.19.5, pp. 240–265.

You read the narrative selectively. The questions to keep in mind:

- **Clocking.** Which clock drives this peripheral? Is it gated? What is the max input clock and the resulting peripheral clock? (For the RP2040 GPIO, the SIO is clocked by `clk_sys` at the system clock, default 125 MHz — see §2.19.3.)
- **Reset state.** What does the peripheral look like out of reset? On the RP2040, every GPIO comes up as input with the pull resistors disabled. You have to actively configure it to be useful.
- **Modes and configuration sequence.** What is the order of operations? Most peripherals require: 1) enable the clock, 2) deassert reset, 3) configure mode, 4) enable the peripheral. Get the order wrong and the peripheral silently does nothing.
- **Interrupts.** Which IRQ line in the NVIC corresponds? What conditions trigger it?
- **DMA.** Which DMA request lines does this peripheral expose?

You will not internalize all of this in one read. You will internalize the *shape* — "GPIO requires a function-select before it does anything; UART has 4 IRQ sources; PWM lives on the SIO" — and that shape is enough to write your first driver.

### Phase 4 — The electrical characteristics (~10 min)

The last section of any datasheet, often the most important and the most ignored. For the RP2040 it is §5, pp. 615–637.

You read four tables here:

- **Absolute maximum ratings.** Past these, the chip is damaged. Not "may misbehave" — *damaged*. For the RP2040: `V_IO_max = 3.63 V`. If you wire VBUS (5 V) to a GPIO, the chip dies.
- **Recommended operating conditions.** Inside these, the chip works as specified. Outside, it is undefined. `VDD_IO = 1.8 V to 3.3 V; T_amb = -20 °C to +85 °C`.
- **DC characteristics.** Per-pin: `VOH`, `VOL`, `IOH`, `IOL`, `VIH`, `VIL`, `IIN_leak`. The Pi Pico W's 3.3 V GPIO source 12 mA max per pin (datasheet §5.2) — enough for one LED, not enough for a relay coil.
- **AC / timing characteristics.** Setup, hold, propagation delays. For a GPIO toggle: the pin transition is fast enough (~50 ns rise/fall at 12 mA load) that you can clock a 1 MHz signal cleanly. For a UART at 921600 baud, you must obey the clock-recovery margin in §4.2.7.

When you start designing your own boards in Week 21, every line of these tables becomes a board-level constraint. For now: just know they exist, and that your 12 mA pin cannot drive a motor without a transistor in the middle.

---

## 3. Worked example — finding the RP2040 GPIO function-select table

Concrete drill. You are writing Lab 03 (Week 3, but we plan ahead). You need to know: "to use GP0 as the UART0 TX line, what register bits do I write?" 

Step 1: open the RP2040 datasheet.
Step 2: search the PDF for "function select" or open the table of contents and find §2.19 (IO_BANK0).
Step 3: jump to §2.19.6.1 ("Function Select Table"), p. 244 in the Sep-2024 rev.
Step 4: read the table.

The table tells you, for every GPIO (rows) and every alternate function slot (columns F1…F8), what each slot does. For GP0 the row reads (abbreviated):

```
  GPIO  F1     F2    F3    F4   F5      F6      F7      F8
  GP0   SPI0  UART0 I2C0  PWM  SIO     PIO0    PIO1    USB
        RX    TX    SDA    A   GPIO0   GPIO0   GPIO0   VBUS
```

So to put GP0 in **UART0 TX** mode, you write `2` (F2) to `GPIO0_CTRL.FUNCSEL`. The `GPIO0_CTRL` register lives at `IO_BANK0_BASE + 0x004`. In C that becomes:

```c
#define IO_BANK0_BASE   0x40014000u
#define GPIO0_CTRL      (*(volatile uint32_t *)(IO_BANK0_BASE + 0x004))
GPIO0_CTRL = 2u;            // 2 = UART0 TX, see §2.19.6.1, p. 244
```

Three lines of C. A 600-page datasheet. The other 597 pages would tell you the same thing, but only the function-select table tells you it for *this* register, *this* bit field, with the legal values to write. That is what a datasheet is for.

---

## 4. The signal envelope — reading electrical specs

Every digital signal on a real board has a voltage and a timing envelope. The RP2040 GPIO output envelope, from §5.2, looks like this:

```
   3.3V ──┐                    ┌──── VOH min = 2.30 V at IOH = 12 mA
          │                    │      (= "guaranteed high" output)
          │     ▁▁▁▁▁▁▁▁▁     │
          │   ▁▁         ▁▁   │
          │ ▁▁             ▁▁ │
   0.0V ──┴──                ──┴──── VOL max = 0.40 V at IOL = 12 mA
                                          (= "guaranteed low" output)

         t-rise              t-fall
         ≤ 50 ns @ 12 mA       ≤ 50 ns @ 12 mA
         (slew rate "fast")    (slew rate "fast")
```

Two reads from that envelope:

1. The pin is *guaranteed* to be at least 2.30 V when driving high into a 12 mA load. If the LED you wired pulls 30 mA, the pin will sag below the spec — and the chip may or may not still drive it correctly. The "12 mA" matters.
2. The pin transitions in ≤ 50 ns. If you toggle the pin at 10 MHz (100 ns period), you have ≤ 50 ns of transition and ≤ 50 ns of stable level on each half-cycle. Above ~5 MHz, the rise/fall starts to dominate. Plan accordingly when designing a fast peripheral.

Datasheet engineers spell out the envelope so software engineers can plan against the worst case. Tutorial writers usually do not. C7 will.

---

## 5. Five questions to answer in 10 minutes

A drill for Wednesday. Open the RP2040 datasheet (Sep-2024 rev). Answer all five:

1. What is the maximum sustained system clock, and what does it require? (Hint: §2.15.6, p. 211.)
2. On the Pi Pico W, which GPIO is wired to the on-board LED? (Hint: not the RP2040 datasheet — the *Pico W* datasheet, p. 5.)
3. How many DMA channels does the RP2040 have, and how many DREQ lines? (Hint: §2.5.1, p. 84.)
4. What is the reset state of the `SIO_GPIO_OUT_EN` register? (Hint: §2.3.1.7, p. 33.)
5. The UART0 RX pin defaults to which GPIO, in which AF slot, and what value gets written to `FUNCSEL` to select it? (Hint: §2.19.6.1, p. 244.)

If you cannot answer all five in 10 minutes, you are not yet fluent at random-access datasheet reading. Repeat the drill on Friday with the STM32F446 reference manual (RM0390, ST). By Week 3 it should be reflex.

---

## 6. Common reading errors

A non-exhaustive list, every one of which we have seen students hit in cohorts:

- **Reading "typical" as "guaranteed."** No. Typical is the manufacturer's bench measurement on a good die at 25°C. Guaranteed is what the chip will do across the temperature range and process corners. Always design to min/max.
- **Confusing the chip datasheet and the board datasheet.** The RP2040 datasheet describes the silicon. The Pi Pico W datasheet describes the carrier board. The on-board LED, USB connector, regulator, and CYW43 wiring are *board*-level concerns. Look in the board datasheet.
- **Trusting "out of the box" defaults.** Some peripherals come up enabled at reset; most do not. Always check the reset value column.
- **Assuming bit numbering.** ARM datasheets are bit-zero-LSB. Some legacy parts (notably PIC and some 8051 derivatives) number bits MSB-first. The RP2040 is sane. Verify on any new family.
- **Skipping the clock-gating table.** A peripheral whose clock is gated off does *exactly nothing*. The single most common "my peripheral does not work" cause, in every cohort, is a forgotten `CLOCKS_WAKE_EN0` bit.
- **Reading registers but not the narrative.** Registers tell you *what* you can write. The narrative tells you in what *order*. Get the order wrong and the registers do not save you.

---

## 7. Self-check

Without re-reading:

1. State the four phases of datasheet reading in order. What is the goal of each?
2. What is the difference between "absolute maximum ratings" and "recommended operating conditions"? Why does the distinction matter?
3. The RP2040 GPIO drives 12 mA at VOH ≥ 2.30 V. If you wire an LED that needs 30 mA forward current, what does the pin's actual VOH look like, and what mitigation does the datasheet recommend?
4. Where in the RP2040 datasheet would you start, if you wanted to write a polled UART TX driver from scratch?
5. Why do you not read a datasheet linearly?
6. State the C-language register-write idiom: given `BASE = 0x40014000u` and `OFFSET = 0x004u`, write a one-line C statement that writes `2u` to that location, with `volatile` correctness.

---

## Further reading

- **RP2040 datasheet** §2.19 (IO_BANK0, GPIO function select) — pp. 240–265.
- **Pi Pico W datasheet** pp. 4–5 (pinout and on-board LED).
- **STM32F446 reference manual (RM0390)** — different chip, same skeleton. The exercise we will do in Week 7 reads pages 244–266 of RM0390 the same way we just read §2.19 of the RP2040.

Next: this week has no Lecture 4. Move to the [exercises](../exercises/README.md), then to the [bring-up note mini-project](../mini-project/README.md).
