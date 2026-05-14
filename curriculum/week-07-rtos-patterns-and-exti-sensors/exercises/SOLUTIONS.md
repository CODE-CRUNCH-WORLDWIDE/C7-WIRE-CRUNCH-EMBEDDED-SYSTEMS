# Exercises — solutions and notes

This file walks the three Week 7 exercises with the expected outputs, the common pitfalls, and the measurement criteria. Read it *after* you have attempted each exercise on your own bench. The point of the exercises is the hands-on debugging.

---

## Exercise 1 — GPIO EXTI to a binary semaphore

### Expected behaviour

Pressing the button on `GP18` toggles the LED on `GP15`. The Saleae shows `GP21` (ISR-entry marker) rising 50–100 ns after the button-press edge, falling ~500 ns later, and `GP20` (task-wake marker) rising 3–5 µs after `GP21`.

### Latency target

| Statistic | Target on Pico W at 125 MHz |
|-----------|----------------------------:|
| median    |                       3.9 µs |
| p99       |                       6.2 µs |
| max       |                       8.4 µs |

### How to measure

Open Saleae Logic 2, capture 30 seconds at 25 MS/s on `GP20`, `GP21`. Add a "Measurement" view, configure:

- "Edges between two channels": from rising edge of `GP21` to next rising edge of `GP20`.

Press the button 30+ times during the capture. The histogram view shows the distribution. The 99-th percentile is what you commit to your `LATENCY.md`.

### Common pitfall — forgot `portYIELD_FROM_ISR`

If you measure ~500 µs latency instead of ~4 µs, you forgot `portYIELD_FROM_ISR` (or you fed it `pdFALSE`). The semaphore got given, but the consumer task does not run until the next `SysTick` tick, which can be up to 1 ms away. The Saleae trace shows the `GP20` rising edge synchronized to the millisecond boundary — a tell-tale alignment.

### Common pitfall — `xSemaphoreCreateBinary` not given before first take

A binary semaphore created with `xSemaphoreCreateBinary` is in the "taken" state at creation. The *first* `xSemaphoreTake(portMAX_DELAY)` from the task blocks forever (no give has happened). The ISR's first give unblocks it; subsequent takes/gives behave normally. This is correct for *this* exercise (the button has to be pressed before the LED toggles), but a common confusion when you first see the pattern: "why is my task not running before the first press?"

For a "task should run once at startup then wait for events" pattern, use `xSemaphoreCreateBinaryStatic` and prime it with `xSemaphoreGive` *before* `vTaskStartScheduler`.

### Common pitfall — bounce detection

A mechanical button bounces for 5–20 ms after each press; without debounce, one physical press produces 3–10 ISR fires. The exercise has a 20 ms debounce in the task (`last_press_tick` check). If you removed it, the LED would toggle multiple times per press and the latency measurement would still be valid (each ISR fire is a separate event), but the press-counter would over-count.

For more sophisticated debounce, use the SDK's `gpio_set_irq_enabled` with a periodic poll, or a hardware RC filter on the button line. For Week 7 the software debounce is fine.

### Stack budget

| Task   | Configured | High-water (words free) | Peak usage |
|--------|-----------:|------------------------:|-----------:|
| button |        512 |                     452 |         60 |
| idle   |        128 |                     108 |         20 |

The button task uses ~60 words at peak: locals (`now_us`, `lat_us`, `now_tick`, `last_press_tick`, `press_count`), the saved register file across the context switch (~16 words), the `printf` call's stack frame (~24 words). 512 is generous; production code re-sizes to 192.

---

## Exercise 2 — MPU-6050 EXTI to a stream buffer

### Expected behaviour

After init, the consumer task wakes once per 5 ms (200 Hz sample rate) and decodes a full `Mpu6050Frame_t`. The printer task wakes once per second and prints the latest frame plus the dropped-frame count.

At steady state, on a stationary MPU lying flat:

```
ax=     0 ay=     0 az=  8190  gx=     1 gy=     0 gz=    -2  drop=0
ax=    -1 ay=     2 az=  8188  gx=     0 gy=     1 gz=    -2  drop=0
ax=     0 ay=     0 az=  8189  gx=     1 gy=     0 gz=    -1  drop=0
```

The accel-Z reading of ~8190 corresponds to +1 g at the +/-4 g full scale (16384 counts per g, half scale ≈ 8192). The accel-X/Y readings near zero confirm "flat". The gyro readings near zero confirm "stationary" (within ~0.02 dps noise at +/-500 dps full scale = 65.5 counts/dps).

### Trigger-level comparison

Run three configurations, save the Saleae captures, populate the homework table.

#### A) Trigger level 1

Change `#define STREAM_TRIGGER_LEN ((size_t)1u)` and re-flash. On the Saleae, `GP20` toggles ~2800 times per second (14 wakeups × 200 Hz). The CPU at idle (read from `uxTaskGetSystemState` or `vTaskGetRunTimeStats`) is ~22 % - the rest spent in context-switch overhead.

#### B) Trigger level 14 (canonical)

The default in the source. `GP20` toggles ~200 times per second. Idle CPU ~94 %.

#### C) Queue replacement

Replace the stream buffer with `QueueHandle_t g_imu_queue = xQueueCreate(4, sizeof(Mpu6050Frame_t));`. In the ISR, decode the frame inline (4 µs of extra work) and `xQueueSendFromISR`. In the consumer, `xQueueReceive`. The wake rate is the same as B (200 Hz). The ISR is ~1.4 µs vs ~0.6 µs (stream-buffer); the consumer skips the decode step (saving ~4 µs per frame).

Net: B and C are within 1 % of each other on idle CPU. The difference is in the ISR latency: ~0.8 µs faster on the stream buffer. For a 200 Hz sample rate, this is a 0.16 % CPU saving — invisible. For a 100 kHz sample rate it would be 80 µs/sec — visible.

### Common pitfall — missing 4-byte timestamp send

The ISR sends 14 bytes of sensor data, then 4 bytes of timestamp. If the consumer's `xStreamBufferReceive(g_imu_stream, &isr_ts_us, 4, timeout)` runs out of timeout before the timestamp arrives, the timestamp is stale. Symptom: latency reads as a small negative number or a very large positive number (uint32 wraparound).

Fix: the consumer's second receive uses a 1 ms timeout, which is plenty for 4 bytes that were sent within ~10 µs of the first 14. If you see stale timestamps, increase the stream buffer to 320 bytes (a few frames of buffering) and confirm the trigger-level math is right.

### Common pitfall — INT pin not configured

If you set `INT_PIN_CFG` to a non-zero value but forget `INT_ENABLE = 0x01`, no interrupts fire. Symptom: WHO_AM_I succeeds, init returns true, but no GP22 edges ever appear on the Saleae. Read `INT_ENABLE` back over I²C after the config write to confirm.

### Common pitfall — I²C burst from the ISR is poor form

Yes, the ISR does a ~280 µs blocking I²C burst. This is bad practice for production — during those 280 µs, every other interrupt at the same priority (every SDK interrupt by default) is delayed. The exercise accepts this cost for simplicity. The Week 8 mini-project replaces the I²C burst with a DMA-driven non-blocking transfer that consumes ~8 µs of CPU time per sample.

To verify the cost on your bench: add a UART RX interrupt at 921 600 baud and observe the UART RX timing during heavy MPU activity. The UART FIFO is 32 bytes, full at 921 600 baud in ~280 µs, so an unlucky alignment can lose RX bytes. Symptom: UART RX shows occasional dropped characters. This is the cost the deferred-interrupt-with-DMA pattern eliminates.

### Stack budget

| Task     | Configured | High-water (words free) | Peak usage |
|----------|-----------:|------------------------:|-----------:|
| consumer |        768 |                     680 |         88 |
| printer  |       1024 |                     900 |        124 |
| idle     |        128 |                     108 |         20 |

Consumer at 88 words: `buf[14]`, `isr_ts_us`, the local `Mpu6050Frame_t` (32 bytes), the stream-buffer-receive trampoline. Printer at 124 words: the `printf` call alone is ~80 words. Production code keeps the consumer at 192, printer at 320.

---

## Exercise 3 — Priority-inheritance bound

### Derive the bound on paper, first

For H at priority 3, M(T) = { g_guard }, L(T) = { L, idle }. Only L takes g_guard. The longest critical section L holds on g_guard is L_CRITICAL_MS = 50 ms. So:

```
B(H) = 50 ms
```

This is the worst-case blocking time. H's effective deadline is 200 ms (H_PERIOD_MS) but H's own work is 50 µs, so H finishes 50 ms + 50 µs = 50.05 ms after wake at worst. Within budget.

### Measure on the bench, second

Run with `USE_MUTEX = 1`. Capture 60 seconds of `GP10`, `GP11`, `GP12`, `GP13` on the Saleae. Add a "Measurement: time between channels" from rising `GP13` (H blocked) to falling `GP13` (H acquired):

| Statistic | Measured                |
|-----------|------------------------:|
| median    |                  ~25 ms |
| p99       |                  ~25 ms |
| max       |                ~25.4 ms |

Why 25 ms and not 50 ms? Because H wakes 25 ms into L's critical section (the offset `H_WAKE_OFFSET_MS = 25 ms`). So H's *measured* wait is the *remaining* critical section, which is 50 − 25 = 25 ms. The *bound* is 50 ms (which would be the wait if H happened to wake at the start of L's critical section). Both numbers are correct; the bound is the worst case over all possible wake offsets, the measurement is the actual at one specific offset.

If you set `H_WAKE_OFFSET_MS = 1`, the measured wait approaches 49 ms — close to the bound. Try it.

### The semaphore comparison

Set `USE_MUTEX = 0` and re-flash. The Saleae now shows `GP11` (M running) interspersed in `GP13` (H blocked). The measured wait for H is no longer 25 ms; it grows by every M run that fits inside H's wait window.

| Statistic | Measured with binary semaphore |
|-----------|-------------------------------:|
| median    |                          ~55 ms |
| p99       |                          ~58 ms |
| max       |                          ~62 ms |

55 ms = 25 ms (L's remaining) + 30 ms (one full M run that fit). If M happened to wake at a slightly worse offset, you could see M and a partial-M, pushing closer to 60 ms.

The instructive comparison: with a mutex, the measured wait converges to a single value (25 ms here); with a binary semaphore, the wait depends on M's run timing and can grow up to (L's residual + all M runs that fit in H's wait window).

### Commit both captures

`homework/p4-priority-bound-mutex.sal` and `.../p4-priority-bound-semaphore.sal`. The side-by-side is the most instructive single artifact in Week 7 after Challenge 1.

### Common pitfall — using `vTaskDelay` instead of `spin_us`

If L's critical section uses `vTaskDelay(50 ms)`, L *yields* the CPU during its critical section. The kernel sees L as blocked, not running. H's wake during this yield finds the mutex unowned (it is *not* — L still holds it, but is not actually using the CPU). The priority-inheritance protocol still works — L cannot run until H releases, but the timing changes because L was already not consuming CPU.

The exercise uses `spin_us` (a busy-wait on the 1 MHz timer) to keep L actually running on the CPU during its critical section. This matches the textbook scenario.

### Stack budget

| Task | Configured | High-water (words free) | Peak usage |
|------|-----------:|------------------------:|-----------:|
| H    |        512 |                     452 |         60 |
| M    |        512 |                     460 |         52 |
| L    |        512 |                     452 |         60 |
| idle |        128 |                     108 |         20 |

Production: 128 each.

---

## Summary table

| Exercise | Bench measurement target               | Expected on Pico W |
|----------|----------------------------------------|-------------------:|
| 1        | p99 ISR-to-task latency (button -> LED) |              6.2 µs |
| 2A       | Context switches per second @ trig=1   |             2800/s |
| 2B       | Context switches per second @ trig=14  |              200/s |
| 2C       | ISR-from-queue cost per sample          |              1.4 µs |
| 3 mutex  | Measured peak B(H)                      |               25 ms |
| 3 sem    | Measured peak B(H)                      |               58 ms |

If your numbers are within ~20 % of these, you are done with the exercises. Move on to the challenges.
