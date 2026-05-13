# Lecture 1 — GPIO From the Register Up

> **Outcome:** You can name the four register banks that govern a single RP2040 GPIO pin, and write a function that toggles a pin using only `volatile uint32_t *` casts. You can cite the address `SIO_GPIO_OUT_XOR = 0xd000_001c` from memory and explain why writing to it is faster than the equivalent `SIO_GPIO_OUT` read-modify-write.

---

## 1. The job of a GPIO pin, restated

A General-Purpose I/O pin is the simplest peripheral on any MCU. It does exactly four things:

1. **Drive a voltage.** When configured as an output, it pulls the pin to `VDD_IO` (3.3 V on the Pico) or to `GND`, with a current capability typically ≤ 12 mA. This is the LED-blink path.
2. **Sense a voltage.** When configured as an input, it returns `1` if the pin is above `V_IH` (typ. 0.7·VDD = 2.3 V on a 3.3 V rail) and `0` if below `V_IL` (typ. 0.3·VDD = 1.0 V). The 1.3 V hysteresis band is where the Schmitt trigger earns its salary.
3. **Route a peripheral signal.** A GPIO is rarely "just" a GPIO. The same physical pad can be wired to the UART transmitter, the SPI clock, the I2C SDA, the PWM output, or a PIO state machine, all by writing one register field — the **function select** (`FUNCSEL`).
4. **Source an interrupt.** When configured as an input with edge or level detection enabled, the pin can wake the CPU through the NVIC. We will use this in Week 7 properly; this week we will poll.

On the RP2040, all four of those jobs are spread across **four register banks**. This is unusual — most MCU families collapse them into one or two banks — and the result is faster code at the cost of more documentation to read. The four banks are:

| Bank | Base address | Job | Datasheet § |
|---|---:|---|---|
| **SIO** | `0xd000_0000` | Single-cycle I/O — the CPU-local bus for fast read/write/set/clear/xor of the GPIO output and direction lines | §2.3 |
| **IO_BANK0** | `0x4001_4000` | The pin mux — selects which peripheral the pad is routed to (`GPIOn_CTRL.FUNCSEL`), and per-pin input/output overrides | §2.19 |
| **PADS_BANK0** | `0x4001_c000` | The pad control — pull-up / pull-down, drive strength, slew rate, Schmitt trigger, input enable | §2.19.4 |
| **(NVIC / IRQ block)** | `0xe000_e100` (NVIC base in the Cortex-M0+) | Interrupt enable / priority / pending bits — this is the ARM standard, not RP2040-specific | DUI 0662B §4 |

Most embedded engineers who came up on Arduino, the STM32 HAL, or the ESP-IDF have never seen the four-bank split. The mental model that worked there ("call `gpio_init` and `gpio_put` and move on") works on the RP2040 too — but it costs you the ability to read a fault, the ability to make the pin atomic across the two cores, and the ability to hit the 25 MHz toggle rate the silicon is actually capable of. This lecture is the price of admission to those three abilities.

---

## 2. The SIO: where speed lives

The **single-cycle I/O** block at `SIO_BASE = 0xd000_0000` is the most important peripheral on the RP2040 that nobody talks about. It is not on the main AHB-Lite peripheral bus. It is on a private bus directly wired to each Cortex-M0+ core's load-store unit. A write to a SIO register completes in **one cycle**. A write to a normal peripheral register (UART, SPI, I2C) takes 2–4 cycles due to bus arbitration. For GPIO, where you may toggle a pin millions of times per second, that difference matters.

The SIO register map for GPIO is small enough to memorize. From RP2040 datasheet §2.3.1.7, p. 41 (Sep-2024 rev):

```
   Offset   Name                Reset      Access  Description
   0x010    GPIO_OUT            0          RW      GPIO output value (bits [29:0] correspond to GP0..GP29)
   0x014    GPIO_OUT_SET        0          WO      Write 1 to set the corresponding bit in GPIO_OUT
   0x018    GPIO_OUT_CLR        0          WO      Write 1 to clear the corresponding bit in GPIO_OUT
   0x01c    GPIO_OUT_XOR        0          WO      Write 1 to XOR (toggle) the corresponding bit in GPIO_OUT
   0x020    GPIO_OE             0          RW      GPIO output enable (0 = high-Z input, 1 = drive)
   0x024    GPIO_OE_SET         0          WO      Write 1 to set the corresponding bit in GPIO_OE
   0x028    GPIO_OE_CLR         0          WO      Write 1 to clear the corresponding bit in GPIO_OE
   0x02c    GPIO_OE_XOR         0          WO      Write 1 to XOR the corresponding bit in GPIO_OE
   0x004    GPIO_IN             -          RO      Current input value at the pin (bits [29:0])
```

Four observations to internalize:

- **`GPIO_OUT_XOR` at `0xd000_001c` is the fastest way to blink an LED on this chip.** Writing `1u << 25` to it toggles GP25; no read, no mask, no read-modify-write race. The address is famous enough that you should know it on sight.
- **The SET / CLR / XOR aliases are write-only.** Reading them returns garbage. They exist to let you set or clear *individual bits* without disturbing the others, in one cycle, atomically. The bare `GPIO_OUT` register is read-write but loses you the atomicity guarantee.
- **`GPIO_OE` is the output enable.** Out of reset, every GPIO is high-Z (input). You must write `GPIO_OE_SET = 1u << 25` before `GPIO_OUT_SET = 1u << 25` will do anything visible at the pin. Forget this and your LED stays dark; this is the single most common Week 2 trap.
- **`GPIO_IN` at `0xd000_0004` is read-only.** It returns the live, *post-Schmitt-trigger* value at the pad. The reset value is "don't care" — it depends on whatever the pin is connected to at boot.

The whole point of the SIO is that these registers sit one cycle from the load-store unit. A `LDR R0, =0xd000_001c` followed by a `STR R1, [R0]` will toggle a GPIO in **two cycles** at 125 MHz — that is a **62.5 MHz toggle rate** in theory, ~25 MHz in practice once you account for the loop overhead. The SDK's `gpio_xor_mask()` call adds ~10 cycles of function-call overhead. We will measure this in Exercise 1.

---

## 3. The atomic alias pattern

The SET/CLR/XOR pattern that lives in the SIO is **not** unique to GPIO. Every peripheral register on the RP2040 — UART, SPI, I2C, ADC, PWM, even DMA — has the same four-alias structure. The pattern is uniform across the chip and saves you a read-modify-write whenever you want to flip individual bits.

For any peripheral register at offset `R` from a peripheral's base address:

```
   base + R + 0x0000   normal (RW)         — write replaces all 32 bits
   base + R + 0x1000   XOR alias (WO)      — write XORs with current value
   base + R + 0x2000   SET alias (WO)      — write 1-bits become 1
   base + R + 0x3000   CLR alias (WO)      — write 1-bits become 0
```

This is documented in RP2040 datasheet §2.1.2 (Atomic Register Access), p. 18. It is implemented in the bus matrix, not in software. The two writes "set bit 3" and "clear bit 7" on the same register, on two cores, in two parallel cycles, will not race — because each one goes to a different address and the bus matrix serializes them.

You will use this pattern dozens of times this week and hundreds of times across C7. Memorize the four offsets.

> **An aside on UARTCR:** the UART control register at `UART0_BASE + 0x030` has its SET alias at `0x4003_4000 + 0x030 + 0x2000 = 0x4003_6030`. To enable just the TX bit (`UARTEN | TXE = bits 0 and 8`) without disturbing whatever else the register holds, you write `0x101` to `0x4003_6030`. This idiom appears verbatim in `pico-sdk/src/rp2_common/hardware_uart/uart.c`.

---

## 4. IO_BANK0: the function-select register, and what "GPIO" really means

A pin on the RP2040 is not, by default, a GPIO. It is a **pad** wired to one of **eight functions** (F1 through F8), only one of which is "act like a software-controlled output." The function-select table on p. 244 of the datasheet (§2.19.6.1) lists, for every pin, which function corresponds to which F-slot:

```
   GPIO   F1     F2      F3    F4   F5     F6     F7     F8
   GP0    SPI0   UART0   I2C0  PWM  SIO    PIO0   PIO1   USB
          RX     TX      SDA   CHA  GPIO0  GPIO0  GPIO0  VBUS_DETECT
   ...
   GP15   SPI1   UART0   I2C1  PWM  SIO    PIO0   PIO1   —
          CS     CTS     SCL   CHB  GPIO15 GPIO15 GPIO15
   ...
   GP25   SPI1   UART1   I2C0  PWM  SIO    PIO0   PIO1   —
          CS     RTS     SDA   CHB  GPIO25 GPIO25 GPIO25
```

The per-pin register that owns this choice is `GPIOn_CTRL`, at offset `0x004 + n * 0x008` from `IO_BANK0_BASE = 0x4001_4000`. So `GPIO15_CTRL` lives at `0x4001_4000 + 0x004 + 15 * 0x008 = 0x4001_407c`. The field of interest is `FUNCSEL` in bits [4:0]. Write `5` to make the pin a SIO GPIO (which is what you want for a software-controlled pin); write `2` to make it UART0 CTS; write `1` to make it SPI1 CS.

From RP2040 datasheet §2.19.6.1, p. 247:

```
   GPIOn_CTRL (offset 0x004 + n*0x008 from IO_BANK0_BASE)

   Bit     Field           Reset  R/W  Description
   [31:30] IRQOVER         0      RW   IRQ output override
   [29:28] (reserved)      -      -    -
   [17:16] INOVER          0      RW   Input override (force input low/high to peripheral)
   [15:14] (reserved)      -      -    -
   [13:12] OEOVER          0      RW   Output-enable override (force OE low/high)
   [11:10] (reserved)      -      -    -
    [9:8]  OUTOVER         0      RW   Output override (force OUT low/high)
   [7:5]   (reserved)      -      -    -
   [4:0]   FUNCSEL         0x1f   RW   Function select. 0x1f = NULL (pin disconnected).
```

The reset value of `FUNCSEL` is `0x1f` — **NULL**, which means the pad is disconnected from every internal peripheral. Out of reset, your GPIO pin does nothing. This is a deliberate design choice: pins do not unintentionally drive on power-up.

To "use GP15 as a software-controlled output," the full register-write sequence is:

```c
/* Step 1. Set FUNCSEL = 5 (SIO) on GP15. */
*(volatile uint32_t *)(0x40014000u + 0x004u + 15u * 0x008u) = 5u;

/* Step 2. Enable the output driver in SIO. */
*(volatile uint32_t *)(0xd0000000u + 0x024u) = 1u << 15;   /* SIO_GPIO_OE_SET, bit 15 */

/* Step 3. Drive the output high. */
*(volatile uint32_t *)(0xd0000000u + 0x014u) = 1u << 15;   /* SIO_GPIO_OUT_SET, bit 15 */

/* Step 4. Drive the output low. */
*(volatile uint32_t *)(0xd0000000u + 0x018u) = 1u << 15;   /* SIO_GPIO_OUT_CLR, bit 15 */
```

This is what `gpio_init(15); gpio_set_dir(15, GPIO_OUT); gpio_put(15, 1);` compiles to in the SDK, plus ~30 cycles of overhead for argument-checking, jump-table lookups, and the abstraction over the two GPIO banks. We will read the SDK source in Lecture 3 and confirm this.

---

## 5. PADS_BANK0: pulls, drive strength, slew, schmitt

The pad-control bank at `PADS_BANK0_BASE = 0x4001_c000` owns the *electrical* behavior of each pin. From RP2040 datasheet §2.19.4, p. 294:

```
   PADS_BANK0_GPIOn (offset 0x004 + n*0x004 from PADS_BANK0_BASE)

   Bit  Field         Reset  R/W  Description
   [7]  OD            0      RW   Output disable (overrides OE; 1 = pad always high-Z output)
   [6]  IE            1      RW   Input enable (1 = receiver buffer powered)
   [5:4] DRIVE        1      RW   Drive strength: 0=2mA, 1=4mA, 2=8mA, 3=12mA
   [3]  PUE           0      RW   Pull-up enable
   [2]  PDE           0      RW   Pull-down enable
   [1]  SCHMITT       1      RW   Schmitt-trigger receiver
   [0]  SLEWFAST      0      RW   Slew rate: 0=slow, 1=fast
```

Three values worth memorizing:

- **IE (Input Enable) defaults to 1.** Even when the pad is configured as output, the receiver buffer is on. This costs ~50 µA per pin. For a low-power design, clear `IE` on every output-only pin. We will revisit in Week 18.
- **DRIVE defaults to 4 mA.** The on-board LED is happy at 4 mA; an external 5 mm LED with a 220 Ω current-limit resistor wants 8 mA; a relay coil wants a transistor in between and not "DRIVE = 3" wishful thinking.
- **PUE / PDE are pull-up / pull-down enables.** They are mutually exclusive in practice — enabling both makes the pin sit at ~1.65 V, which is in the "neither high nor low" gap. The SDK's `gpio_pull_up(n)` writes to `PADS_BANK0_GPIOn` bit 3 (PUE) via the SET alias. We will need this for the button on GP15.

The pad bank is **not** in the SIO. A write to a pad register takes ~4 cycles (it is on the AHB-Lite peripheral fabric). You will not toggle these registers in a hot loop; you write them once at init time.

---

## 6. The full GPIO init sequence, three ways

To make this concrete, here is the same operation — "make GP15 a software-controlled output, pull-down enabled, 8 mA drive, slew slow, then drive it high" — written three ways. All three produce the same machine behavior; they differ in line count, readability, and what the compiler can prove about them.

### Way 1 — Raw register writes, `volatile uint32_t *`

```c
#define IO_BANK0_BASE   0x40014000u
#define PADS_BANK0_BASE 0x4001c000u
#define SIO_BASE        0xd0000000u

#define GPIO15_CTRL     (*(volatile uint32_t *)(IO_BANK0_BASE   + 0x004u + 15u * 0x008u))
#define PADS_GPIO15     (*(volatile uint32_t *)(PADS_BANK0_BASE + 0x004u + 15u * 0x004u))
#define SIO_GPIO_OE_SET (*(volatile uint32_t *)(SIO_BASE + 0x024u))
#define SIO_GPIO_OUT_SET (*(volatile uint32_t *)(SIO_BASE + 0x014u))

static inline void gp15_output_init(void) {
    GPIO15_CTRL      = 5u;            /* FUNCSEL = SIO */
    PADS_GPIO15      = (1u << 6)      /* IE = 1 (default; redundant) */
                     | (2u << 4)      /* DRIVE = 8 mA */
                     | (1u << 2)      /* PDE = 1 (pull-down) */
                     | (1u << 1);     /* SCHMITT = 1 (default; redundant) */
    SIO_GPIO_OE_SET  = 1u << 15;
    SIO_GPIO_OUT_SET = 1u << 15;
}
```

This is what you will write in Exercise 1. It is honest, ugly, and the fastest path to comprehension. Every line corresponds to one row in the datasheet.

### Way 2 — The Pico SDK, the SDK way

```c
#include "hardware/gpio.h"
#include "hardware/structs/pads_bank0.h"

static inline void gp15_output_init_sdk(void) {
    gpio_init(15);
    gpio_set_dir(15, GPIO_OUT);
    gpio_pull_down(15);
    gpio_set_drive_strength(15, GPIO_DRIVE_STRENGTH_8MA);
    gpio_put(15, 1);
}
```

This is what you will use in 95% of your shipped code. It is what the cohort reviewer expects to see. We will read the SDK source for each of these calls in Lecture 3 and confirm they compile to the register writes of Way 1.

### Way 3 — The SDK's `*_hw` structs

```c
#include "hardware/structs/sio.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/pads_bank0.h"

static inline void gp15_output_init_structs(void) {
    io_bank0_hw->io[15].ctrl   = 5u;                      /* FUNCSEL = SIO */
    pads_bank0_hw->io[15]      = (1u << 6) | (2u << 4)
                               | (1u << 2) | (1u << 1);
    sio_hw->gpio_oe_set        = 1u << 15;
    sio_hw->gpio_set           = 1u << 15;
}
```

The struct path is the SDK's preferred middle ground: type-checked, IDE-completable, no magic numbers. The `sio_hw` pointer is `(sio_hw_t *)SIO_BASE`. The compiler produces the same `STR` instructions Way 1 produces, but the source reads almost like the datasheet table.

The C7 voice is: **Way 1 to learn, Way 3 to ship, Way 2 for prototypes.** We will keep all three alive.

---

## 7. The atomic toggle, by way of motivation

To close the loop, here is why the SET/CLR/XOR aliases exist. Consider this naive "toggle GP15" function:

```c
static inline void gp15_toggle_naive(void) {
    uint32_t v = *(volatile uint32_t *)(SIO_BASE + 0x010u);   /* read GPIO_OUT */
    v ^= (1u << 15);                                          /* flip the bit  */
    *(volatile uint32_t *)(SIO_BASE + 0x010u) = v;            /* write back    */
}
```

Three problems with this:

1. **Three instructions, not one.** Load, XOR, store. At 125 MHz that is 24 ns per toggle, ~21 MHz max rate, not the silicon's actual ~25 MHz capability.
2. **Not atomic across the two cores.** If core 0 reads `GPIO_OUT`, then core 1 also reads, both XOR their target bit, both write back — the second write wins and the first core's bit is lost. The classic read-modify-write race.
3. **Touches all 32 bits.** Bits you did not intend to touch are written with whatever was in the register at the read. If another piece of code is writing bit 14 in parallel, you may stomp on it.

The XOR-alias version fixes all three:

```c
static inline void gp15_toggle_fast(void) {
    *(volatile uint32_t *)(SIO_BASE + 0x01cu) = 1u << 15;     /* GPIO_OUT_XOR */
}
```

One instruction. One cycle. Atomic in the bus matrix. Touches only bit 15. This is the address `0xd000_001c` in the lecture title.

You will use `SIO_GPIO_OUT_XOR` literally every week of C7 from now on. Memorize the address.

---

## 8. What to do this week with what you learned

Three concrete drills:

1. **Exercise 1** asks you to toggle GP15 by direct register write at 1 Hz, then at the fastest rate you can produce, and to capture both on a logic analyzer. The fastest rate should be in the 10–25 MHz range. If you get 1 MHz, your loop has an `sleep_us(1)` in it; remove it.
2. **The mini-project** asks for at least one direct register write somewhere in the firmware. The fault-model card requires you to name which register and why you chose the register path over the SDK call there.
3. **The register-table artifact** asks you to list every register address, offset, reset value, and access type you touched. This is the C7 brand signature for Week 2 — analogous to the Week 1 bring-up note. Build the habit now.

---

## 9. Up next

Lecture 2 — UART Bit Timing and Handshake — covers the same depth of treatment for the PL011 UART block. You will compute the integer + fractional baud-rate divisor for 115200 baud from the 125 MHz peripheral clock, and you will see, on a Saleae trace, that your computed value produces a bit period within 0.16% of 8.681 µs.

Before you move on, write the four register addresses on a sticky note and put it on your monitor:

```
   SIO_GPIO_OUT       0xd000_0010
   SIO_GPIO_OUT_SET   0xd000_0014
   SIO_GPIO_OUT_CLR   0xd000_0018
   SIO_GPIO_OUT_XOR   0xd000_001c
```

You will use these four addresses for the next 22 weeks.
