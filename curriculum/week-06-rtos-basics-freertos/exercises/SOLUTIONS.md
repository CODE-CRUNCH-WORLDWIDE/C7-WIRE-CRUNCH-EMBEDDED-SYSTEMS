# Exercises — solutions and notes

This file walks the three exercises with the expected outputs, the common pitfalls, and the measurement criteria. Read it *after* you have attempted each exercise on your own bench. The point of the exercises is the hands-on debugging, not the answers.

---

## Exercise 1 — Blinky as two tasks

### Expected behaviour

Two LEDs blink independently:

- GP14 toggles every 250 ms (2 Hz overall — one full on-off cycle in 500 ms).
- GP15 toggles every 100 ms (5 Hz overall — one full cycle in 200 ms).

On a logic analyzer with both lines captured for 1 second, you should see GP14 with 2 complete cycles and GP15 with 5 complete cycles. The transitions are not synchronized — every 500 ms (the LCM of the two half-periods), GP14 and GP15 cross simultaneously, but otherwise the edges land independently.

### Stack budget

`uxTaskGetStackHighWaterMark` for each task at steady state, with `configCHECK_FOR_STACK_OVERFLOW = 2`:

| Task   | Configured (words) | High-water (words free) | Peak usage (words) |
|--------|-------------------:|------------------------:|-------------------:|
| blinkA |                256 |                     208 |                 48 |
| blinkB |                256 |                     208 |                 48 |
| idle   |                128 |                     108 |                 20 |

48 words of usage is reasonable: a few words of locals (`xLastWakeTime`, `xPeriod`, `level`), the saved register file for the context switch (~16 words), and a handful of words of margin for the scheduler's bookkeeping. The configured 256 leaves ~200 words of headroom, which is excessive for production but appropriate for an exercise.

For shipping firmware, you would re-size to 128 words and re-test.

### Common pitfall — using `vTaskDelay` instead of `vTaskDelayUntil`

If you used `vTaskDelay(pdMS_TO_TICKS(250))` instead of `vTaskDelayUntil`, the period of the 2 Hz blinker is *actually* `250 ms + work_time`. The `gpio_put` plus context-switch overhead is about 1 µs — small enough that the drift is invisible on a 1 second capture but real over an hour. Use `vTaskDelayUntil` for periodic work; it is what the API exists for.

### Common pitfall — task body returns

If you wrote:

```c
static void vBlinkATask(void *p) {
    for (int i = 0; i < 10; i++) { ... }
    /* falls through here */
}
```

The function returns into the kernel's task-exit trampoline. On FreeRTOS this calls `configASSERT(0)` (with `configCHECK_FOR_STACK_OVERFLOW != 0`), which lands you in `vAssertCalled` or a wild jump. The fix is `for (;;) { ... }` — a task never returns.

### Verification on the bench

1. Flash the .uf2.
2. Both LEDs start blinking at boot.
3. Open the Saleae software, capture both GP14 and GP15 for 1 second at 1 MHz.
4. Use the "measure" tool: GP14 high duration should be 249.5–250.5 ms; GP15 high duration should be 99.5–100.5 ms.
5. The tolerance is bounded by the tick rate (1 kHz = 1 ms) and the scheduler overhead. ±1 ms is acceptable.

---

## Exercise 2 — Button to LED via a queue

### Expected behaviour

- Press the button: the LED toggles.
- Each press appears as one line of UART output: `button edge_id=N tick=T drops_full=0 drops_bounce=K`.
- `drops_bounce` increments by 5–20 per press depending on how clean your button is. The 25 ms debounce window is aggressive enough to suppress mechanical bounce on a typical tactile switch.
- `drops_full` stays at 0 in normal use. To exercise it: change `QLEN` to 1 in the code, rebuild, then press the button rapidly while the LED task is held (e.g. deliberately add `vTaskDelay(pdMS_TO_TICKS(500))` after the LED toggle to slow it down). You will see `drops_full` increment.

### Latency budget on the logic analyzer

Capture GP15 (button line) and GP14 (LED) on the Saleae. Measure the time from GP15's falling edge to GP14's transition:

| Component                                | Cycles | Time @ 125 MHz |
|------------------------------------------|-------:|---------------:|
| GPIO interrupt -> ISR entry (M0+)        |    ~16 |        ~130 ns |
| ISR work (xQueueSendFromISR)             |   ~120 |        ~960 ns |
| portYIELD_FROM_ISR -> PendSV             |    ~12 |         ~96 ns |
| PendSV context switch                    |    ~84 |        ~670 ns |
| Task resume -> xQueueReceive returns     |    ~50 |        ~400 ns |
| gpio_put                                 |     ~6 |         ~50 ns |
| **Total**                                | ~**288** |     **~2.3 us** |

On a clean capture you should see latency around 2–4 µs from press to LED. If you see > 100 µs, you may have a higher-priority task busy-running and the LED task is waiting for its turn — check that no other task is at higher priority than `PRIO_LED_TASK`.

### Common pitfall — missing `portYIELD_FROM_ISR`

If you omit `portYIELD_FROM_ISR(xHigherPriorityTaskWoken)` at the end of the ISR, the LED task does not wake immediately. It waits for the next tick (up to 1 ms later) before being dispatched. The system still works but latency jumps by ~1 ms — visible on the logic analyzer as a delay between button edge and LED edge. Always call `portYIELD_FROM_ISR`.

### Common pitfall — using `xQueueSend` instead of `xQueueSendFromISR`

If you used the non-`FromISR` variant from the ISR, FreeRTOS calls `configASSERT(0)` (with assertions enabled) and you land in the assert hook. If assertions are disabled, the kernel silently corrupts its waiters list — typically the system survives for a few seconds, then crashes inside `xTaskResumeAll` or `prvProcessReceivedCommands`. The compiler does not warn you. Read the API docs and double-check every `*FromISR` suffix.

### Common pitfall — not initializing the queue handle before enabling the ISR

If you enable the GPIO interrupt before `xQueueCreate` returns, the first press calls `xQueueSendFromISR` with a NULL `xButtonQueue` and crashes. The exercise code's `main()` orders these correctly: `xQueueCreate` first, then `gpio_set_irq_enabled_with_callback`. Match the order.

### Common pitfall — wrong button wiring

The exercise expects active-low (button between GP15 and GND, internal pull-up enabled, press = falling edge). If you wired active-high (button between GP15 and 3V3, internal pull-down, press = rising edge), set `GPIO_IRQ_EDGE_RISE` instead of `EDGE_FALL`. Both work; the code as written assumes active-low.

---

## Exercise 3 — UART protected by a mutex

### Expected behaviour

**With `WITH_MUTEX = 1`** (mutex protection enabled):

```
[fast 0]
[fast 1]
[fast 2]
[fast 3]
[fast 4]
[fast 5]
[fast 6]
[fast 7]
[fast 8]
[fast 9]
[slow 0]
[fast 10]
[fast 11]
...
```

Every line is whole. The interleaving between fast and slow is at line boundaries, never within a line.

**With `WITH_MUTEX = 0`** (racy version):

```
[fast 0]
[fa[slow 0]
st 1]
[fas[slow 1]
[fast 2]
...
```

Lines from the two tasks interleave byte-by-byte. The exact pattern depends on the tick at which the scheduler context-switches between the two tasks; the racy interleavings are reproducible but their exact form is not. The UART receiver software (minicom, the Saleae UART decoder) sees what looks like corruption.

### How the race actually happens

Both tasks have the same priority (2). The kernel time-slices between them on each tick (1 ms). The fast task calls `uart_puts("[fast N]\r\n")` which writes characters into the PL011's FIFO one at a time via a sequence of `uart_putc` calls. If the scheduler preempts the fast task mid-string, the next tick can schedule the slow task, which calls its own `uart_puts("[slow M]\r\n")` and starts feeding its characters into the FIFO — interleaved with the fast task's pending bytes.

The mutex closes the window: while the fast task holds the mutex, the slow task blocks on `xSemaphoreTake` and does not write to the UART. Only after the fast task's `uart_puts` returns and `xSemaphoreGive` runs does the slow task acquire the mutex and write.

### What a binary semaphore would have done — and why it would be wrong

Suppose you used `xSemaphoreCreateBinary` (with a pre-emptive `xSemaphoreGive` to start it in the available state) instead of `xSemaphoreCreateMutex`. The lines would still be whole in *this* exercise, because there is no priority inversion — both tasks are at the same priority. The visible behaviour is identical to the mutex version.

The problem is hidden. Add a third task at priority 1 (lower than the print tasks) that occasionally writes to the UART. Now if the high-priority task ever blocks waiting for the binary semaphore held by a low-priority task, and a medium-priority task preempts the low-priority task, the high-priority task is starved indefinitely. With a mutex, the low-priority task is temporarily promoted and the medium-priority task cannot preempt it.

Challenge 1 demonstrates this explicitly.

### Verification on the bench

1. Set `WITH_MUTEX = 0`, build, flash, capture 5 seconds of UART on the Saleae.
2. Decode the UART line in the Saleae software (Analyzers -> Async Serial).
3. Export the decoded characters as CSV.
4. Look for `]` characters not immediately followed by `\r\n` — those are the interleaving points.
5. Set `WITH_MUTEX = 1`, rebuild, flash, capture again.
6. Decoded output should now have every line whole. Check with `awk` that every `]` is followed by `\r\n`:
   ```
   awk '{print}' decoded.csv | grep -E '\][^\r]' | wc -l
   ```
   Should be 0 for the mutex version, > 0 for the racy version.
7. Commit both CSVs to your exercise repo with names `ex03-racy.csv` and `ex03-mutex.csv`.

### Common pitfall — taking the mutex from an ISR

You cannot use `xSemaphoreTake` from an ISR — the task-context API will assert or corrupt. If you want to log from an ISR, defer the log message to a queue and let a logger task drain the queue under the mutex. The mini-project's gatekeeper-task stretch goal is one way; alternatively, `xSemaphoreTakeFromISR` with `block = 0` works for *trying* to take without blocking, but if you must succeed you have to go through a task.

### Common pitfall — holding the mutex for too long

If your "critical section" is "format a 80-byte string with snprintf and then send it over UART one byte at a time at 115200 baud (~7 ms for the full string)", you hold the mutex for ~7 ms. That is fine for two same-priority tasks but problematic if a higher-priority task also wants the UART — the higher-priority task is blocked for up to 7 ms even with priority inheritance.

The fix: do the `snprintf` *outside* the critical section (into a per-task buffer), then take the mutex only across the `uart_puts` of the formatted buffer. Or better: move the UART to a dedicated gatekeeper task (mini-project stretch goal) and let every other task `xQueueSend` formatted strings.

The exercise as written keeps `snprintf` inside the critical section for simplicity; you should re-factor for a production driver.

---

## Cross-cutting lessons

After the three exercises, you should have direct bench experience with:

1. **The basic task model.** Two tasks running concurrently from one core, scheduled by the kernel, with independent timing.
2. **ISR-to-task hand-off through a queue.** The fundamental interrupt-driven RTOS pattern.
3. **Resource protection with a mutex.** And the bug you would have hit if you had used a binary semaphore instead.

Take the captures and the stack budgets you measured during these exercises and stash them in a `EXERCISES.md` in your week-06 repo. They are the empirical baseline against which Challenge 1 and the mini-project will be compared.
