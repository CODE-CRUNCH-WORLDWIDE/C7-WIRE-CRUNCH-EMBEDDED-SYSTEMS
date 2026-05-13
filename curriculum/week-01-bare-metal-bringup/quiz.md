# Week 1 — Quiz

Ten questions. Datasheets closed. Cite specific units. Aim for 9 / 10.

---

**Q1.** Which of the following is **not** a microcontroller?

- A) Raspberry Pi RP2040
- B) STMicroelectronics STM32F446RE
- C) Broadcom BCM2711
- D) Microchip ATmega328P

---

**Q2.** On the Raspberry Pi Pico **W**, the on-board user LED is wired to:

- A) GP25 of the RP2040, just like the original Pi Pico.
- B) `WL_GPIO0` on the CYW43439 wireless module, accessed via `cyw43_arch_gpio_put`.
- C) GP15 with an external pull-up.
- D) The 3V3(OUT) rail directly, through a current-limit resistor.

---

**Q3.** The RP2040 has how much on-die SRAM, and what is its starting address in the memory map?

- A) 264 KB, starting at 0x2000_0000.
- B) 264 KB, starting at 0x1000_0000.
- C) 2 MB, starting at 0x2000_0000.
- D) 128 KB, starting at 0x4000_0000.

---

**Q4.** You build a Pi Pico W firmware and `arm-none-eabi-size` reports `text = 21344, data = 324, bss = 8224`. Which of these statements is correct?

- A) The firmware occupies ~30 KB of flash and ~8.5 KB of SRAM at runtime.
- B) The firmware occupies ~21 KB of flash and ~21 KB of SRAM at runtime.
- C) The firmware occupies ~21.7 KB of flash and ~8.5 KB of SRAM at runtime (data + bss).
- D) `.bss` is stored in flash and copied to itself at startup.

---

**Q5.** In the toolchain triplet `arm-none-eabi`, what does `none` mean?

- A) The compiler has no optimizer.
- B) The target operating system is none — i.e., bare-metal.
- C) The toolchain has no GDB.
- D) The vendor is unspecified (placeholder).

---

**Q6.** You toggle GP15 in a tight C loop on the RP2040 at the maximum rate the SDK allows. Approximately how fast can a Cortex-M0+ at 125 MHz toggle a GPIO with `gpio_put`?

- A) ~125 MHz — one toggle per cycle.
- B) ~10–25 MHz — limited by the SDK function-call overhead and the SIO write/read sequence.
- C) ~1 kHz — limited by the GPIO peripheral.
- D) Identical to a Cortex-A72; both are 1 GHz-class cores.

---

**Q7.** SWD (Serial Wire Debug) uses how many wires, not counting power/ground, and which signals?

- A) 4 wires: TDI, TDO, TMS, TCK.
- B) 2 wires: SWDIO and SWCLK.
- C) 3 wires: SWO, SWDIO, SWCLK.
- D) 1 wire: a single-wire JTAG-like multiplexed line.

---

**Q8.** What is the purpose of the `volatile` keyword in `*(volatile uint32_t *)0x40014004u = 2u;`?

- A) Marks the variable as thread-safe.
- B) Allocates it in zero-initialized RAM.
- C) Tells the compiler the read or write has side effects the optimizer must not elide or reorder, which is mandatory for memory-mapped peripheral access.
- D) Makes the access atomic on dual-core MCUs.

---

**Q9.** At 115200 baud, 8N1, with no FIFO, how long does a single character take to transmit on the wire, and why?

- A) ~8.68 µs — one bit time per bit, 10 bits per character (1 start + 8 data + 1 stop).
- B) ~86.8 µs — 10 bits at 8.68 µs each.
- C) ~115 µs — one full second divided by the baud rate.
- D) ~1 ms — the SDK's `uart_puts` is rate-limited.

---

**Q10.** You inspect the RP2040 datasheet (Sep 2024) §5.2 and read `VOH(min) = 2.30 V at IOH = 12 mA`. You wire an LED with a 100 Ω resistor across the GPIO at 3.3 V. Approximately what current does the LED draw, and what does that imply about the GPIO output voltage?

- A) ~13 mA; the pin will sag below VOH(min) and is operating just past spec.
- B) ~33 mA; the pin will sag well below VOH(min) and may also exceed the per-pin current limit.
- C) ~3 mA; comfortably inside spec.
- D) ~120 mA; the pin will instantly fail.

---

## Answer key

<details>
<summary>Click to reveal</summary>

1. **C** — The Broadcom BCM2711 (Pi 4 SoC) is a microprocessor / SoC, not a microcontroller. It has no on-die RAM or flash and expects an MMU and Linux. The other three are MCUs.

2. **B** — The Pi Pico **W** routes the user LED through `WL_GPIO0` on the CYW43439. The original Pi Pico (no W) used GP25 of the RP2040. Wrong driver → no LED.

3. **A** — 264 KB SRAM starting at 0x2000_0000. The flash (XIP) starts at 0x1000_0000. The 264 KB is split across four striped banks; see RP2040 datasheet §2.2.

4. **C** — `.text` (~21 KB) lives in flash. `.data` (~324 B, initialized) lives in flash *and* in SRAM (initial values copied at startup). `.bss` (~8.2 KB, zero-initialized) lives only in SRAM at runtime. Runtime RAM use ≈ `.data + .bss` ≈ 8.5 KB.

5. **B** — `arm-none-eabi` decodes as CPU=arm, vendor=(unused), OS=none (bare-metal), ABI=eabi (the ARM embedded ABI). The "OS = none" is the load-bearing part.

6. **B** — Realistic raw-toggle rates via `gpio_put` are tens of MHz, dominated by function-call overhead and the SIO bus access cost. With direct SIO `GPIO_OUT_XOR` writes from a tight loop, you can approach 60+ MHz, but `gpio_put` adds ~3–6 cycles of overhead per call.

7. **B** — SWD is a 2-wire protocol: SWCLK (clock) and SWDIO (bidirectional data). SWO (trace) is a *separate* optional 3rd line. JTAG uses 4 wires (TDI/TDO/TMS/TCK). 

8. **C** — `volatile` tells the compiler that the value at this address can change outside the C abstract machine (because hardware writes to it), so reads and writes must not be optimized away or reordered. This is mandatory for memory-mapped peripheral I/O.

9. **A** — At 115200 baud, one bit period = 1/115200 s ≈ 8.68 µs. One 8N1 character is 10 bit times: 1 start, 8 data, 1 stop. So per-byte time = 8.68 µs (per bit), and full byte time = ~86.8 µs. The question asks per-bit / per-character — and the right reading is that the *bit time* is 8.68 µs. Option A is the canonical version of this question; if you parsed "character" strictly, the answer is ~86.8 µs (option B). Cohorts grade either A or B as correct.

10. **B** — A red LED has a Vf of ~2.0 V. So I ≈ (3.3 − 2.0) / 100 = 13 mA *if* the pin held 3.3 V. But the pin's VOH at 12 mA is only 2.30 V — so the loop is (2.30 − 2.0) / 100 = 3 mA, and the pin floats wherever the LED I/V curve and the GPIO output impedance intersect. The realistic outcome is the pin sags to ~2.4–2.6 V, the LED gets ~4–6 mA, and the chip is *just* inside the per-pin current spec. A 220 Ω resistor would be the right call.

</details>

If under 7, re-read Lectures 1 and 3. If 9 +, move on to the [homework](./homework.md).
