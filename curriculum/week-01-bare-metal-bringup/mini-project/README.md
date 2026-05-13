# Mini-Project — The Week 1 Bring-Up Note

> Build a Pi Pico W firmware that blinks, talks UART, and reacts to a button. Then publish the **bring-up note** — the one-page truth-telling artifact about what works on your bench, in the C7 voice. Week 2 of [`SYLLABUS.md`](../../../SYLLABUS.md) references this file by name.

This is the practical synthesis of the whole week. It combines Exercise 2 (blink in C), Exercise 3 (the MicroPython comparison numbers), and Challenge 1 (UART) into a single firmware image, then asks you to write the kind of one-page status document a senior firmware engineer writes at the end of every bring-up sprint.

**Estimated time:** 8 hours, spread across Thursday – Saturday.

---

## What you will build

A C firmware for the Raspberry Pi Pico W that, on a single board, simultaneously:

1. **Blinks** the on-board LED at **1.0 Hz** (50% duty cycle).
2. **Prints** `crunch-wire week-01 boot ok t=<seconds_since_boot>` to **UART0** at **115200 8N1** every **1.000 seconds**, where the timestamp is monotonic milliseconds since boot, expressed in seconds with 3-decimal precision.
3. **Reads** the state of a tactile push-button wired between **GP15** and **GND** (active-low, with the internal pull-up enabled). When the button is held, the blink rate **changes to 5 Hz** and the UART line includes the suffix `button=down`. On release, it returns to 1 Hz and the suffix becomes `button=up`.
4. **Boots in < 100 ms** from a clean USB plug-in to the first UART line. Measure this with a stopwatch; sub-100 ms is the bar.
5. **Survives 30 minutes** of continuous operation on USB power, with no missed blinks and no missed UART lines (both verified by a Saleae or `sigrok` capture of any 60-second window).

And a **bring-up note** (`BRING-UP-NOTE.md`) at the repo root that documents all of this in the C7 voice.

---

## Acceptance criteria

- [ ] A new public GitHub repo `c7-week01-bringup-<yourhandle>`.
- [ ] `git clone …` and `cmake -B build -DPICO_BOARD=pico_w . && cmake --build build -j` succeeds with no warnings other than the SDK's own.
- [ ] `build/bringup.uf2` exists and is ≤ 50 KB.
- [ ] Flashed via BOOTSEL, the board demonstrates all five behaviors above.
- [ ] `BRING-UP-NOTE.md` at the repo root contains every required section listed below.
- [ ] `README.md` at the repo root contains setup, build, flash, wiring diagram (ASCII or image), and a "What works / What does not" header — see template below.
- [ ] A `traces/` directory contains at least:
  - One Saleae or PulseView capture of GP15 (the button) and either GP0 (UART TX) or another spare GPIO mirroring the LED, with the UART decoder applied. `.sal` or `.sr` file plus a screenshot.
  - One scope or Saleae screenshot of the LED-mirror GPIO showing the 1 Hz → 5 Hz transition when the button is pressed.
- [ ] A **fault model card** in the bring-up note (template below) covers at least three faults.

---

## The bring-up note structure (required)

Your `BRING-UP-NOTE.md` follows this exact structure, in the C7 voice:

```
# Week 1 Bring-Up Note — <yourhandle>

> Pi Pico W, RP2040, pico-sdk <version>, gcc-arm-none-eabi <version>
> Built on <date> on <macOS Sonoma 14.5 / Ubuntu 22.04 / WSL2 Ubuntu 22.04>
> Tagline: one-sentence status — e.g. "All three behaviors work; UART byte timing within spec; one open question on the bottom rail noise."

## What works

- 1 Hz on-board LED blink, measured period <X.XXX s> on the scope, drift <Y ppm> over 30 minutes.
- UART TX 115200 8N1 on GP0, bit period <8.XX µs measured>, deviation from 8.68 µs target = <Z%>.
- Button on GP15 with internal pull-up: 5 Hz blink while held, returns to 1 Hz within <N ms> of release.
- Boot time, USB plug to first UART line: <T ms>.

## What does not work (or is not yet measured)

- e.g., "I have not verified the boot time below 100 ms; my stopwatch reads ~150 ms but I suspect that includes my reaction."
- e.g., "I do not have a scope, so I have not validated the LED period; I am relying on visual estimation."
- Be honest. The reader's trust scales with what you admit, not with what you claim.

## Fault model

(see template below — at least three rows)

## Bench artifacts

- `traces/uart-decode.sal`     — Saleae capture, UART decoded, two full messages
- `traces/blink-transition.png` — scope screenshot of 1 Hz → 5 Hz on button press
- `notes/size.txt`             — arm-none-eabi-size of the final .elf
- `notes/build.log`            — full CMake + ninja output

## Open questions

- One or two paragraphs of "I don't yet understand why X" — e.g., "When the button is released, the blink rate updates within ~200 ms instead of the next loop iteration. I suspect debounce timing in `gpio_get` but I have not verified."

## Toolchain pinning

- arm-none-eabi-gcc: 13.2.Rel1
- openocd: 0.12.0+dev (Raspberry Pi fork @ <commit hash>)
- pico-sdk: 1.5.1 @ <commit hash>
- probe-rs: 0.24.0
```

---

## The fault model card

Every C7 deliverable from Week 1 onward includes one of these. This is the brand signature. Datasheet-grade discipline starts here:

```
┌─────────────────────────────────────────────────────────────────────┐
│  FAULT MODEL — c7-week01-bringup-<yourhandle>                       │
│                                                                     │
│  Button bouncing on press:    debounce by 20 ms in software         │
│  USB cable disconnected:      hardware reset on re-plug; no state   │
│  UART receiver overrun:       this build is TX-only; OK             │
│  CYW43 init fails (bad SPI):  return 1 from main(); LED stays off   │
│  Power glitch on VBUS:        regulator handles; firmware unaware   │
└─────────────────────────────────────────────────────────────────────┘
```

Minimum three rows. Each row names a fault and the mitigation. "No mitigation; this fault is out of scope for Week 1" is an acceptable answer for one row — be explicit.

---

## Suggested file layout

```
c7-week01-bringup-<yourhandle>/
├── README.md
├── BRING-UP-NOTE.md
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── src/
│   ├── bringup.c              ← main()
│   ├── led.c   led.h          ← on-board LED wrapper
│   ├── uart.c  uart.h         ← UART TX wrapper
│   └── button.c button.h      ← GP15 button + debounce
├── notes/
│   ├── size.txt
│   ├── build.log
│   ├── blink-c.md             (from Exercise 2)
│   ├── blink-micropython.md   (from Exercise 3)
│   ├── uart-hello.md          (from Challenge 1)
│   ├── toolchain-install.md   (from Exercise 1)
│   └── week-01-reflection.md  (from Homework 6)
└── traces/
    ├── uart-decode.sal
    ├── uart-decode.png
    └── blink-transition.png
```

You can keep the C in a single `bringup.c` file if you prefer — for ~150 lines of code, splitting is optional. The folder split signals architectural intent for the cohort reviewer.

---

## Suggested order of operations

### Phase 1 — Build skeleton (1 h)

1. Make repo, copy your Exercise-2 `CMakeLists.txt`, add `hardware_uart` to `target_link_libraries`.
2. Wire up `pico_stdlib` UART0 and confirm `printf("hello\n")` reaches your terminal.
3. Make the first commit. Do not skip this commit; you will want a "before" point later.

### Phase 2 — Behaviors one at a time (3 h)

- **Behavior 1 (LED blink).** Reproduce Exercise 2's 1 Hz blink. Verify on scope.
- **Behavior 2 (UART).** Add `printf` once per second with timestamp from `to_ms_since_boot(get_absolute_time())`. Verify in terminal.
- **Behavior 3 (button).** `gpio_init(15); gpio_set_dir(15, GPIO_IN); gpio_pull_up(15);` then read `gpio_get(15)`. Make the blink rate conditional. Test by jumpering GP15 to GND with a wire (you don't strictly need a button for the first pass).
- **Behavior 4 (boot time).** Measure with a stopwatch by counting "one Mississippi" from cable insertion. If > 100 ms, look at any blocking init you can defer.
- **Behavior 5 (30-min soak).** Plug it in. Walk away. Come back. Look at a 60-second logic capture from somewhere in the middle. Confirm uniformity.

### Phase 3 — Hardware bench-up (1 h)

- Solder or breadboard the actual push-button between GP15 and GND. Verify it still works.
- Set up your USB-serial bridge for the UART. Capture one good Saleae trace at >2 MHz sample rate covering ≥ 2 UART messages and ≥ 1 button transition.

### Phase 4 — The bring-up note (2 h)

- Write `BRING-UP-NOTE.md` following the structure above.
- Cite datasheet sections. Use mono font (JetBrains Mono in the rendered version; backticks in raw Markdown) for every voltage, frequency, pin number, and register address.
- Include the fault-model card. Three rows minimum.
- Be ruthless about what *does not* work or is *not yet measured*. The bring-up note's job is to be true, not to impress.

### Phase 5 — Polish (1 h)

- Write the README. Setup, build, flash, wiring diagram. Drop in `BRING-UP-NOTE.md` as a link at the top.
- Commit traces. Confirm the repo builds from a fresh clone.
- Push.

---

## Rubric

| Criterion | Weight | "Great" looks like |
|-----------|------:|--------------------|
| It builds | 20% | `cmake -B build -DPICO_BOARD=pico_w . && cmake --build build` works on a fresh clone with no edits |
| Behaviors 1–3 work on bench | 25% | Reviewer flashes your `.uf2`, sees the LED, sees the UART, presses the button, sees the rate change. No special handling. |
| Bring-up note quality | 25% | Datasheet-grade voice; units everywhere; honest "what does not work" section; ≥ 3 fault-model rows |
| Bench artifacts | 15% | `.sal` file decodes cleanly; screenshots show the actual data, not just a sketch |
| README + repo hygiene | 10% | A reviewer with no context can flash and run within 15 minutes |
| Code readability | 5% | One job per function; ≤ 30 lines per function; no `volatile` misuse |

---

## Stretch goals

- Add a 7-segment display or an SSD1306 OLED over I2C and put the seconds-since-boot count on it. The Week-7 lab will revisit I2C properly; you are previewing.
- Implement a software debounce in `button.c` (start with 20 ms of stable-low before reporting "pressed"). Document the off-by-one you almost certainly hit on the first attempt.
- Switch the LED-blink-rate logic to be edge-triggered instead of level-triggered (the rate changes once on press, once on release, instead of "while held"). This is a small but real state-machine.
- Implement the UART output with `uart_putc_raw` + a hand-rolled `itoa` instead of `printf`. Compare the binary sizes. `printf` is ~6 KB; `itoa` is ~80 bytes. Document the delta.
- Wire up a debug probe and step through the boot sequence under GDB. Note where `cyw43_arch_init()` finishes and how many cycles it costs.

---

## Why this matters

This is the artifact a future hiring manager will ask you to show. "What's your most basic embedded project?" — this is it. It looks small. It is small. But every cell of it is at the level of detail that says "this person knows what a bring-up note is."

The bring-up note specifically is the C7 brand signature. Week 2 of the syllabus will assume you have this artifact. Week 23 (the on-call drill) assumes you can write something *like* this under pressure on an unfamiliar board. Build the muscle now.

---

## Submission

Commit. Push. Open a PR in the cohort review tracker linking to your repo and to your `BRING-UP-NOTE.md`. A peer or TA will sign off on:

1. The repo builds.
2. The bring-up note is in the C7 voice (units, citations, fault model).
3. The traces decode and prove the behaviors.

Once signed off, you are cleared for Week 2.
