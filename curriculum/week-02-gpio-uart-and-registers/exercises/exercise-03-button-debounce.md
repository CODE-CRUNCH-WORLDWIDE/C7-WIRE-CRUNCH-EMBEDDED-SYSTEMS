# Exercise 3 — Button Debounce

**Time estimate:** ~120 minutes (most of it watching a logic analyzer to *see* the bounce).

## Problem statement

Wire a tactile push-button between GP15 and GND. Write a firmware that detects **press events** (low-going edges) and **release events** (high-going edges) reliably, with a 20 ms software debounce filter, even when the mechanical switch bounces for 5–20 ms on press and 1–5 ms on release. Capture the raw GP15 line on a logic analyzer, see the actual bounce, and then re-capture after your debounce is in place and confirm exactly one event per physical press.

## Acceptance criteria

- [ ] A new directory `c7-week02-button-debounce/` containing `button.c`, `CMakeLists.txt`, and `pico_sdk_import.cmake`.
- [ ] A tactile push-button is wired between **GP15 and GND**, with the internal pull-up enabled (so GP15 reads high when the button is released, low when pressed).
- [ ] You capture a Saleae or PulseView trace of GP15 **without debounce**, zoomed to a single button press, showing at least 3 bounce edges within the first 5 ms. Keep this trace; it is part of the deliverable.
- [ ] Your firmware implements a **20 ms debounce** using a state machine (states: `STABLE_HIGH`, `MAYBE_PRESSED`, `STABLE_LOW`, `MAYBE_RELEASED`) sampled every 1 ms via `time_us_64()` polling. No `sleep_ms` inside the state machine.
- [ ] Each clean press produces exactly **one** "pressed" event (printed over UART0 at 115200 8N1 as `EVT press t=<ms>\r\n`) and each clean release produces exactly **one** "released" event (`EVT release t=<ms>\r\n`).
- [ ] You run a **1000-press soak test** (you can use a wire or a finger; document which). Across 1000 physical presses you observe ≥ 998 correct press events and ≥ 998 correct release events. Less than 99.8% is a failed acceptance.
- [ ] A `notes/button-debounce.md` documents the wiring diagram, the state machine (as ASCII or a small diagram), the before/after Saleae traces, the soak-test results, and at least three named failure modes.

## Hints

<details>
<summary>Wiring</summary>

A tactile push-button (any 4-pin 6x6 mm tactile switch) has two pairs of legs internally shorted to each other. When you press, the two pairs connect. The simplest wiring:

```
   GP15  ──────────┐
                   |
                   |
                   ◯  (button leg 1)
                   ◯  (button leg 3, internally bonded to leg 1 — DO NOT use this pair)
                   ╳
                   ╳ (closes when pressed)
                   ◯  (button leg 2, opposite side)
                   ◯  (button leg 4, internally bonded to leg 2)
                   |
                   |
   GND   ──────────┘
```

Easier: use legs **diagonally opposite** (1 and 4 if numbered from top-left clockwise). Diagonal legs are always different pairs. Adjacent legs may be the same pair.

The 10 kΩ internal pull-up on GP15 means: no external resistor needed. The line idles high (~3.3 V) when the button is up; it goes to 0 V when pressed.

</details>

<details>
<summary>The C source (`button.c`)</summary>

```c
/* C7 · Crunch Wire — Week 02 — Button debounce on GP15.
 *
 * Hardware: Raspberry Pi Pico W
 * Pin:      GP15 with internal pull-up, button to GND
 * UART:     UART0 (GP0/GP1) at 115200 8N1 for event log
 *
 * Build:
 *   cmake -B build -DPICO_BOARD=pico_w .
 *   cmake --build build -j
 * Flash:
 *   drag build/button.uf2 to RPI-RP2 in BOOTSEL mode
 */

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include <stdio.h>

#define BUTTON_PIN     15
#define DEBOUNCE_MS    20u
#define SAMPLE_US      1000u   /* 1 ms sampling */

typedef enum {
    STATE_STABLE_HIGH,    /* button up, line high */
    STATE_MAYBE_PRESSED,  /* saw a low; counting down debounce timer */
    STATE_STABLE_LOW,     /* button down, line low */
    STATE_MAYBE_RELEASED, /* saw a high; counting down debounce timer */
} btn_state_t;

static void log_event(const char *evt, uint32_t t_ms) {
    char buf[48];
    int n = snprintf(buf, sizeof buf, "EVT %s t=%lu\r\n", evt, (unsigned long)t_ms);
    if (n > 0) {
        uart_write_blocking(uart0, (const uint8_t *)buf, (size_t)n);
    }
}

int main(void) {
    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);

    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    uart_puts(uart0, "crunch-wire w02 button-debounce ready\r\n");

    btn_state_t state = STATE_STABLE_HIGH;
    uint32_t stable_count = 0;       /* counts samples in candidate state */
    uint64_t last_sample_us = time_us_64();

    while (1) {
        uint64_t now_us = time_us_64();
        if (now_us - last_sample_us < SAMPLE_US) continue;
        last_sample_us = now_us;

        bool raw_low = (gpio_get(BUTTON_PIN) == 0);
        uint32_t t_ms = (uint32_t)(now_us / 1000u);

        switch (state) {
        case STATE_STABLE_HIGH:
            if (raw_low) {
                state = STATE_MAYBE_PRESSED;
                stable_count = 1;
            }
            break;
        case STATE_MAYBE_PRESSED:
            if (raw_low) {
                if (++stable_count >= DEBOUNCE_MS) {
                    state = STATE_STABLE_LOW;
                    log_event("press", t_ms);
                }
            } else {
                state = STATE_STABLE_HIGH;
            }
            break;
        case STATE_STABLE_LOW:
            if (!raw_low) {
                state = STATE_MAYBE_RELEASED;
                stable_count = 1;
            }
            break;
        case STATE_MAYBE_RELEASED:
            if (!raw_low) {
                if (++stable_count >= DEBOUNCE_MS) {
                    state = STATE_STABLE_HIGH;
                    log_event("release", t_ms);
                }
            } else {
                state = STATE_STABLE_LOW;
            }
            break;
        }
    }
    return 0;
}
```

</details>

<details>
<summary>The state diagram (ASCII)</summary>

```
   ┌─────────────────────────────────────────────────────────────────┐
   │                                                                 │
   │   ┌──────────────┐  raw_low   ┌──────────────────┐              │
   │   │ STABLE_HIGH  │ ─────────► │  MAYBE_PRESSED   │              │
   │   │              │            │  count = 1       │              │
   │   └──────────────┘            └──────────────────┘              │
   │          ▲                       │      ▲                       │
   │          │                       │      │ raw_low,              │
   │          │ count = DEBOUNCE_MS   │      │ count++               │
   │          │ "release" event       │      │ count < 20            │
   │   ┌──────┴───────────┐           │      │                       │
   │   │ MAYBE_RELEASED   │           │      │                       │
   │   │ count = 1        │           │      │                       │
   │   └──────────────────┘  raw_high │ count = DEBOUNCE_MS          │
   │          ▲             ┌─────────┘ "press" event                │
   │          │             │                                        │
   │          │             ▼                                        │
   │          │     ┌───────────────┐                                │
   │          └─────┤  STABLE_LOW   │                                │
   │   raw_high     │               │                                │
   │                └───────────────┘                                │
   │                                                                 │
   └─────────────────────────────────────────────────────────────────┘
```

The state machine is a 4-state Mealy machine. Transitions out of `MAYBE_PRESSED` back to `STABLE_HIGH` happen if the line goes back high before `DEBOUNCE_MS` samples have accumulated — that is the bounce filter.

</details>

<details>
<summary>Capturing the bounce without debounce</summary>

Before you add the state machine, write a simpler firmware that just logs `gpio_get(15)` every 100 µs. Capture GP15 on a Saleae at 4 MHz sample rate. Zoom in on a single button press.

You will see something like (idealized):

```
   ─────────┐  ┌─┐ ┌──┐    ┌─┐  ┌────────────────────────
            │  │ │ │  │    │ │  │
            │  │ │ │  │    │ │  │
            └──┘ └─┘  └────┘ └──┘
            <───── ~8 ms total ─────>
```

Six or seven bounce edges across ~8 milliseconds. The exact number and duration depend on the switch you bought; cheap tactiles bounce more, name-brand Omrons bounce less. Document yours.

</details>

<details>
<summary>The 1000-press soak test</summary>

Manual press: ~3 seconds per press at human speed. 1000 presses = ~50 minutes. Get coffee.

Faster: use a 1 Hz square wave from a function generator wired to GP15 (with the pull-up disabled, so the generator can drive it). 1000 cycles = 1000 seconds = 17 minutes. If you do not have a function generator, you can use a *second* Pi Pico W as a stimulus source, driving its own GPIO at 1 Hz wired into your DUT's GP15.

Better: write the test result to UART and to a file with `mpremote cp :events.log .` (if you also include a tiny MicroPython logger on a second board). But for ~17 minutes of bench time, just watch the count on the terminal.

Acceptance is **≥ 998 of 1000** press events and ≥ 998 release events. Less is a fail; more than the actual press count is *also* a fail (it means your debounce is too short and the bounce produced spurious events).

</details>

## What to capture

In `notes/button-debounce.md`:

```
# Exercise 3 — Button Debounce

## Wiring

[ASCII diagram or photo: GP15 → button leg 1; GND → button leg 4 (diagonal)]
Internal pull-up: enabled via gpio_pull_up(15) → PADS_BANK0_GPIO15 bit 3.

## State machine

[Diagram, 4 states, transitions labeled. ~10 lines of ASCII.]

## Bounce trace (before debounce)

[Saleae screenshot: GP15, zoomed to one press, ~7 bounce edges visible in ~8 ms]
Sample rate: 4 MHz
Bounce duration measured: 8.3 ms
Number of edges in bounce window: 7

## Clean trace (after debounce)

[Saleae screenshot: GP15 raw and one event-marker GPIO toggled in `log_event`]
Bounce edges on GP15: same as before (the hardware doesn't change)
Event-marker toggles: exactly 1 per physical press

## Soak test results

  Total presses: 1000
  press events received: 1000 (100.0%)
  release events received: 999 (99.9%)
  Missed: 1 release on press 723; cause unknown (suspect a < 20 ms tap)

## Failure modes (≥ 3)

  - Contact bounce on press:        filtered by 20 ms debounce.
  - Contact bounce on release:      filtered by 20 ms debounce.
  - EMI / nearby motor coupling:    not tested on this exercise; would need
                                    a shielded cable or hardware filter.
  - Stuck low (water on PCB):       state machine reports continuous "low";
                                    no event. Would need a stuck-state
                                    timeout in production.
  - Two presses faster than 40 ms:  filtered as one press. Acceptable for
                                    a human user; not for a barcode trigger.

## Reflection

  - Why 20 ms? Because cheap tactiles bounce up to 15 ms on press; 20 ms
    gives 5 ms of headroom. A 50 ms debounce feels sluggish to a human.
  - What is the failure mode if you set DEBOUNCE_MS to 2? You get
    multiple events per press, one per bounce edge.
  - What is the failure mode if you set DEBOUNCE_MS to 200? You miss
    deliberate double-taps. The "noise" is now signal.
```

## Stretch goals

- Switch the debounce strategy to an **integrator** ("shift register") approach: every sample, shift in the current line state into a 32-bit register; declare "pressed" when the register has been all-0 for 20 consecutive samples, "released" when all-1 for 20. Compare the code complexity and the response latency.
- Add a second button on GP14 with the same state machine. Make the system a 2-button input — log `EVT button=15 press` and `EVT button=14 press`. Confirm both work independently (one button held does not block the other's detection).
- Replace the 1 ms polling with an actual GPIO IRQ on GP15's falling edge. The IRQ handler bumps a "saw an edge" flag; the main loop confirms with a 20 ms wait + re-read. This is the production pattern. We will see it again in Week 7.
- Wire an LED to GP14 and have it mirror the **debounced** button state. This is the visual confirmation that the state machine works; if the LED flickers on press, the debounce is wrong.

## Why this matters

Every product with a button has a debounce. Most products with a debounce have a bug in it. The classic failure: a user double-clicks faster than your debounce allows, and the second click is silently dropped. Or: a user presses softly and the line bounces below the Schmitt-trigger threshold for 30 ms, and your 20 ms debounce reports two events. Or: EMI from a nearby motor injects spikes that your software cannot distinguish from a press.

This exercise builds the muscle that says "I have seen the bounce, I have measured it, I have filtered it, and I have a state machine that handles it deterministically." Every later week — Week 7 (IRQ-driven inputs), Week 16 (sensor fusion), Week 23 (fleet on-call) — relies on this muscle.

By Sunday, "debounce" should mean "20 ms low-pass on a 4-state machine with edge detection," not "a magic word my Arduino code uses." Build the muscle.
