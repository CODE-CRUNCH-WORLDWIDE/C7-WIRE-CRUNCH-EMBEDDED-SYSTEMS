# Mini-Project — The Week 2 Register Table

> Build a Pi Pico W firmware that emits a structured UART log over UART0 and blinks the on-board LED at a button-controlled rate, with **at least one direct register write** somewhere in the firmware. Then publish the **register table** — the one-page truth-telling artifact about every register address you wrote to, in the C7 voice. Week 3 of [`SYLLABUS.md`](../../../SYLLABUS.md) references this file by name.

This is the practical synthesis of Week 2. It combines Exercise 1 (toggle by register), Exercise 2 (UART echo), and Exercise 3 (button debounce) into a single firmware image, then asks you to write the kind of register-level documentation a senior firmware engineer writes when bringing up a new peripheral on a new board. The register table is the Week 2 brand signature, analogous to the Week 1 bring-up note.

**Estimated time:** 6 hours, spread across Wednesday – Saturday.

---

## What you will build

A C firmware for the Raspberry Pi Pico W that, on a single board, simultaneously:

1. **Blinks the on-board LED** at a rate that depends on the state of a tactile push-button wired to **GP15**:
   - Button **released** (line high, debounced): **1.0 Hz** blink (500 ms on, 500 ms off).
   - Button **pressed and held** (line low, debounced): **5.0 Hz** blink (100 ms on, 100 ms off).
   - The transition between rates happens **within 100 ms** of the debounced press/release event.
2. **Emits a UART log** over UART0 at **115200 8N1** (GP0 = TX, GP1 = RX) once per second, with the following format:

   ```
   crunch-wire w02 t=<seconds>.<ms> led=<on|off> rate=<1Hz|5Hz> button=<up|down>
   ```

   For example: `crunch-wire w02 t=12.345 led=on rate=1Hz button=up\r\n`.

3. **Emits an event line** for each debounced button transition, on the same UART, of the form:

   ```
   EVT <press|release> t=<seconds>.<ms>
   ```

4. **Includes at least one direct register write** for a non-trivial operation. Acceptable examples:
   - Toggle the LED-mirror GPIO (use GP14 as a hardware mirror of the LED state) using `SIO_GPIO_OUT_XOR` at `0xd000_001c` directly, without `gpio_xor_mask`. Document why in a comment.
   - Configure the UART line-control register `UARTLCR_H` at `0x4003_402c` with a raw write, citing §4.2.7.6, p. 436.
   - Set the PAD drive strength on GP14 with a raw write to `PADS_BANK0_GPIO14`, citing §2.19.4, p. 294.
   - Any other register write you can justify in a one-line comment. The point is to show you can drop down when needed and document it.

5. **Survives a 30-minute soak** of continuous operation, with no missed log lines and no missed button events. Verified by a Saleae or `sigrok` capture of any 60-second window.

And a **register table** (`REGISTER-TABLE.md`) at the repo root that documents every register touched in the firmware, in the C7 voice.

---

## Acceptance criteria

- [ ] A new public GitHub repo `c7-week02-register-table-<yourhandle>`.
- [ ] `git clone …` and `cmake -B build -DPICO_BOARD=pico_w . && cmake --build build -j` succeeds with no warnings other than the SDK's own.
- [ ] `build/w02.uf2` exists and is ≤ 60 KB.
- [ ] Flashed via BOOTSEL, the board demonstrates all five behaviors above.
- [ ] The firmware contains **at least one** `*(volatile uint32_t *)0x…` write that is not behind a `#define`-only wrapper. Grep-able: `grep -E 'volatile uint32_t \*' src/*.c` returns ≥ 1 line of actual code (not just a typedef or a header).
- [ ] `REGISTER-TABLE.md` at the repo root contains every required section (see template).
- [ ] `README.md` at the repo root contains setup, build, flash, wiring diagram (ASCII or image), and a "What works / What does not" header.
- [ ] A `traces/` directory contains at least:
  - One Saleae or PulseView capture of GP0 (UART TX) and GP15 (button) with the **Async Serial 115200 8N1** decoder applied. Two complete log lines plus at least one `EVT press` or `EVT release` line decoded in the capture.
  - One scope or Saleae screenshot of the LED-mirror GPIO (GP14) showing the 1 Hz → 5 Hz transition on a button press.
- [ ] A **fault model card** in `REGISTER-TABLE.md` (template below) covers at least three faults.

---

## The register table structure (required)

Your `REGISTER-TABLE.md` follows this exact structure, in the C7 voice:

```
# Week 2 Register Table — <yourhandle>

> Pi Pico W, RP2040, pico-sdk <version>, gcc-arm-none-eabi <version>
> Built on <date> on <macOS Sonoma 14.5 / Ubuntu 22.04 / WSL2 Ubuntu 22.04>
> Tagline: one-sentence status — e.g. "All five behaviors work; raw write to SIO_GPIO_OUT_XOR drives GP14 mirror; UART divisor matches measured bit period within 0.05%."

## Registers touched

| Register | Address | Offset from base | Value(s) written | Datasheet § / page | Why we wrote it raw (vs SDK) |
|---|---:|---:|---:|---|---|
| GPIO15_CTRL | 0x4001_407c | +0x07c from IO_BANK0 | 5 (FUNCSEL = SIO) | §2.19.6.1, p. 247 | via SDK gpio_init |
| PADS_BANK0_GPIO15 | 0x4001_c040 | +0x040 from PADS_BANK0 | 0x4a (IE, PUE, SCHMITT) | §2.19.4, p. 294 | via SDK gpio_pull_up |
| SIO_GPIO_OE_SET | 0xd000_0024 | +0x024 from SIO | 1u << 14 (GP14 output) | §2.3.1.7, p. 43 | via SDK gpio_set_dir |
| SIO_GPIO_OUT_XOR | 0xd000_001c | +0x01c from SIO | 1u << 14 (every blink) | §2.3.1.7, p. 42 | RAW: hot path, ~10 cycles saved per toggle |
| UART0_IBRD | 0x4003_4024 | +0x024 from UART0 | 67 | §4.2.7.4, p. 435 | via SDK uart_init |
| UART0_FBRD | 0x4003_4028 | +0x028 from UART0 | 52 | §4.2.7.5, p. 435 | via SDK uart_init |
| UART0_LCR_H | 0x4003_402c | +0x02c from UART0 | 0x70 (8N1, FIFO on) | §4.2.7.6, p. 436 | via SDK uart_set_format |
| UART0_CR | 0x4003_4030 | +0x030 from UART0 | 0x301 (UARTEN, TXE, RXE) | §4.2.7.7, p. 437 | via SDK uart_init |

Minimum 8 rows. Every row cites a datasheet section and page number.

## The raw write — what and why

Quote the actual line of code from your firmware:

  /* GP14 mirrors the LED state, toggled by raw XOR for ~2-cycle hot path.
     See Week 2 Lecture 1, §7. SDK gpio_xor_mask costs ~10 cycles per call. */
  *(volatile uint32_t *)0xd000001cu = 1u << 14;

Cite which file and line. Justify why you chose the register path over the SDK call here. A good justification: "this is the hot path, fires 10x per second in 5 Hz mode, the SDK overhead is wasted." A bad justification: "I wanted to use a raw write." Reviewers will probe.

## What works

  - 1 Hz LED blink with button up, 5 Hz with button down. Measured period
    1.000 s ± 1 ms (1 Hz) and 200 ms ± 2 ms (5 Hz) on a Saleae trace.
  - UART0 at 115200 8N1: measured bit period 8.68 µs on the Saleae,
    well inside the PL011's 2% tolerance.
  - Button debounce: 20 ms low-pass filter, 4-state machine. 100% press
    detection rate over 100 manual presses; no spurious events.
  - Boot time, USB plug to first UART line: < 200 ms (measured by stopwatch).
  - 30-minute soak: ran from <start time> to <end time>, no missed lines.

## What does not work (or is not yet measured)

  - e.g., "I have not measured the absolute drift of the 1 Hz blink over
    30 minutes; the visible period is 1.000 s ± 1 ms in any 60-second
    window but I cannot rule out a slow drift."
  - e.g., "I do not have a hardware push-button on hand; I am jumpering
    GP15 to GND with a wire, which produces a sharper edge than a real
    button would. My debounce works on real buttons too but my soak data
    is from wire-pulls, not presses."
  - Be honest. The reader's trust scales with what you admit.

## Fault model

(see template below — at least three rows)

## Bench artifacts

  traces/uart-decode.sal       Saleae capture, UART decoded, 2 lines + 1 EVT
  traces/blink-transition.png  scope/Saleae screenshot of 1 Hz → 5 Hz
  notes/sio-register-table.md  Problem 1 from homework
  notes/pl011-divisor-table.md Problem 2 from homework
  notes/disassembly-compare.md Problem 3 from homework
  notes/baud-stress.md         Problem 4 from homework
  notes/atomic-vs-rmw.md       Problem 5 from homework
  notes/week-02-reflection.md  Problem 6 from homework

## Open questions

  One or two paragraphs of "I don't yet understand why X." Example:
  "When I switch from 1 Hz to 5 Hz mode, the first 5 Hz cycle appears
  to be ~120 ms instead of 100 ms. I suspect the state-machine sample
  timer is still on its 1 ms cadence and the rate change is being
  applied at the next sample boundary; I have not verified."

## Toolchain pinning

  arm-none-eabi-gcc:  13.2.Rel1
  pico-sdk:           1.5.1 @ <commit hash>
  cmake:              3.27.x
  picotool:           1.1.x
```

---

## The fault model card

Every C7 deliverable from Week 1 onward includes one of these. The format from Week 1 carries through:

```
┌─────────────────────────────────────────────────────────────────────┐
│  FAULT MODEL — c7-week02-register-table-<yourhandle>                │
│                                                                     │
│  Button bouncing on press:    20 ms software debounce, 4-state SM   │
│  UART receiver overrun:       TX-only this build; OK                │
│  Button held > 1 minute:      no event repeats; LED stays at 5 Hz   │
│  Wrong clk_peri (PLL change): uart_set_baudrate would recompute;    │
│                               our raw IBRD/FBRD writes would not.   │
│                               Out of scope this week.               │
│  CYW43 LED driver fails:      LED dark, UART log still produced;    │
│                               visible by terminal, invisible to eye │
│  Power glitch on VBUS:        regulator handles; firmware unaware   │
└─────────────────────────────────────────────────────────────────────┘
```

Minimum three rows. Each row names a fault and the mitigation. "No mitigation; out of scope for Week 2" is an acceptable answer for one row — be explicit.

---

## Suggested file layout

```
c7-week02-register-table-<yourhandle>/
├── README.md
├── REGISTER-TABLE.md
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── src/
│   ├── w02.c                    ← main()
│   ├── led.c   led.h            ← on-board LED + GP14 mirror; contains the raw XOR write
│   ├── uart.c  uart.h           ← UART TX + log_event helpers
│   └── button.c button.h        ← GP15 button + 4-state debounce SM
├── notes/
│   ├── sio-register-table.md    (Problem 1)
│   ├── pl011-divisor-table.md   (Problem 2)
│   ├── disassembly-compare.md   (Problem 3)
│   ├── baud-stress.md           (Problem 4)
│   ├── atomic-vs-rmw.md         (Problem 5)
│   └── week-02-reflection.md    (Problem 6)
└── traces/
    ├── uart-decode.sal
    ├── uart-decode.png
    └── blink-transition.png
```

You can keep all the C in a single `w02.c` if you prefer; ~250 lines fits. The folder split signals architectural intent.

---

## Suggested order of operations

### Phase 1 — Build skeleton (1 h)

1. Make repo, copy your Exercise-3 button debounce code as the starting point.
2. Add UART0 init via `uart_init(uart0, 115200)` and the boot banner.
3. Confirm the basic blink + button + UART path all work; commit a "behaviors-restored" baseline.

### Phase 2 — Add the structured log (1 h)

- Implement `log_per_second()` — produce the one-line-per-second log over UART, with timestamp, LED state, rate, and button state. Use `snprintf` for safety; do not roll your own integer formatter this week.
- Implement `log_event()` — fire on each debounced press/release transition. Already half-written from Exercise 3.

### Phase 3 — Add the raw register write (30 min)

- Pick one place to drop to raw register writes. The C7-preferred place is the LED-mirror GPIO toggle: every blink, write `SIO_GPIO_OUT_XOR = 1u << 14` directly. Comment in the source citing Lecture 1 §7 and the page reference.
- Verify with `arm-none-eabi-objdump -d build/w02.elf | grep d000001c` that the address is baked into the firmware as a literal.

### Phase 4 — Bench validation (1.5 h)

- Wire the button, plug in the USB-serial bridge, open a terminal.
- Capture two Saleae traces: one of the UART showing two log lines + one press event; one of the LED-mirror pin showing the 1 Hz → 5 Hz transition.
- Run the 30-minute soak. Walk away. Come back. Verify the log lines are still coming and the count is monotonic.

### Phase 5 — The register table (1.5 h)

- Write `REGISTER-TABLE.md` following the structure above.
- Cite every address. Use mono font for every voltage, frequency, pin number, and register address.
- Include the fault-model card. Minimum three rows.
- Be ruthless about "what does not work or is not yet measured."

### Phase 6 — Polish (30 min)

- Write the `README.md`. Setup, build, flash, wiring diagram. Link to `REGISTER-TABLE.md`.
- Commit traces. Confirm the repo builds from a fresh clone.
- Push.

---

## Rubric

| Criterion | Weight | "Great" looks like |
|-----------|------:|--------------------|
| It builds | 15% | `cmake -B build -DPICO_BOARD=pico_w . && cmake --build build` works on a fresh clone with no edits |
| Behaviors 1–4 work on bench | 20% | Reviewer flashes your `.uf2`, sees the log, presses the button, sees the rate change, the log reflects it |
| Raw register write quality | 15% | The chosen write is justified in a comment, cites a datasheet page, and would survive a senior code review |
| Register-table quality | 25% | Datasheet-grade voice; units everywhere; ≥ 8 rows; honest "what does not work" section; ≥ 3 fault-model rows |
| Bench artifacts | 15% | `.sal` files decode cleanly; screenshots show the actual data, not just a sketch; the LED transition is visible |
| README + repo hygiene | 5% | A reviewer with no context can flash and run within 15 minutes |
| Code readability | 5% | One job per function; ≤ 30 lines per function; no `volatile` misuse |

---

## Stretch goals

- Add a **second button** on GP12 that toggles the log verbosity between "compact" (one line per second, current behavior) and "verbose" (one line per 100 ms with extra fields). Document the new state diagram.
- Implement the **UART RX path** so that typing `r` in the terminal forces a re-print of the current state immediately. This proves both directions of the UART.
- Replace `snprintf` in the log path with a hand-rolled `itoa` + concat. Measure the binary-size delta with `arm-none-eabi-size`. Expected: ~6 KB smaller. Document it.
- Wire a third GPIO (say, GP11) to act as an **ISR timing marker**: toggle it high at the start of `log_per_second()` and low at the end. Capture both that pin and UART TX on a Saleae and confirm the log call completes inside 5 ms.
- Port the same firmware to **MicroPython** (a 50-line `main.py`) and measure: how does the binary size, RAM usage, and boot time compare? Add the table to `notes/week-02-reflection.md`. This is the same comparison drill from Week 1 — repeat it because the muscle compounds.

---

## Why this matters

This is the artifact a future hiring manager will ask you to show when they want to know "do you understand registers?" The C7 register table specifically is the brand signature for Week 2, analogous to the Week 1 bring-up note. Week 3 of the syllabus (Linker Scripts & Startup) builds on the assumption that you can name the four register banks of an RP2040 GPIO pin in your sleep.

The raw register write requirement is deliberate: a senior firmware engineer must be able to drop down to direct register I/O *and* be able to justify when not to. By Sunday, the answer to "when do I drop to a raw write?" should be a one-paragraph essay, not a feeling.

A C7 graduate at Week 2 can read a peripheral register table, configure the peripheral by hand, verify it on a logic analyzer, and document it in a way a teammate can review. By Week 12, you will do this on an unknown peripheral on an unfamiliar Cortex-M board in 90 minutes. The muscle starts here.

---

## Submission

Commit. Push. Open a PR in the cohort review tracker linking to your repo and to your `REGISTER-TABLE.md`. A peer or TA will sign off on:

1. The repo builds.
2. The register table cites datasheet pages and lists ≥ 8 registers.
3. The raw register write is justified in a comment.
4. The Saleae traces decode and prove the behaviors.

Once signed off, you are cleared for Week 3.
