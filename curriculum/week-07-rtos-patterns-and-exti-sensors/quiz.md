# Week 7 — Quiz

Ten questions covering deferred-interrupt handling, NVIC priority on Cortex-M0+, stream buffers vs queues vs notifications, the priority-inheritance bound, gatekeeper tasks, and deadlock by mutex order. Closed-book; the answers are at the bottom but read them only after attempting all ten.

Aim for 8 / 10 to consider yourself ready to start Week 8. If you score below 6, re-read Lecture 2 and re-run Exercise 2.

---

## 1.

Inside a GPIO rising-edge interrupt handler on the RP2040, you call `xStreamBufferSendFromISR(handle, data, len, &xHigher)` and then return. The send succeeded and `xHigher` is `pdTRUE`. The task that was blocked on `xStreamBufferReceive` was at a higher priority than the task you interrupted. What happens next?

a) The blocked task runs immediately on ISR return because the kernel detects the priority change.
b) The blocked task runs at the next `SysTick` tick (up to 1 ms later) because there is no explicit yield.
c) Nothing — `*FromISR` calls do not affect scheduling.
d) The blocked task runs immediately on ISR return only if you also call `portYIELD_FROM_ISR(xHigher)` before returning.

---

## 2.

On Cortex-M0+ with the standard FreeRTOS Pico SDK port, what is the default value of `configMAX_SYSCALL_INTERRUPT_PRIORITY` and what does it mean for an SDK-installed interrupt at default priority?

a) 0x00 — only the highest-priority interrupts can call FreeRTOS APIs.
b) 0x40 — interrupts at 0x40 or lower numerical value cannot call FreeRTOS APIs.
c) 0xC0 — interrupts at 0xC0 or higher numerical value can call FreeRTOS APIs; this includes every SDK-installed interrupt (which are all at 0xC0 by default).
d) 0xFF — no interrupts can call FreeRTOS APIs; the kernel runs entirely in thread mode.

---

## 3.

You are designing the ISR-to-task path for a sensor that fires at 1 kHz and delivers a 4-byte sample. The task processes one sample per wake. Which primitive minimizes context-switch overhead?

a) A `xStreamBufferSendFromISR` with trigger level 1.
b) A `xStreamBufferSendFromISR` with trigger level 4.
c) A `xQueueSendFromISR` to a length-4 queue of `uint32_t`.
d) A `xTaskNotifyGiveFromISR` with `eIncrement`.

---

## 4.

A stream buffer is single-producer / single-consumer. Why does this restriction allow `xStreamBufferSendFromISR` to skip the kernel critical section that `xQueueSendFromISR` requires?

a) Stream buffers do not modify any kernel data; they only update userspace state.
b) The head index is written only by the producer and the tail index is written only by the consumer; a memory barrier (`__DMB`) suffices to order reads and writes without disabling interrupts.
c) Stream buffers are lock-free because they use compare-and-swap atomics, which Cortex-M0+ supports natively.
d) The kernel guarantees no other interrupt can fire during a stream-buffer operation.

---

## 5.

Three FreeRTOS tasks H (priority 3), M (priority 2), L (priority 1) and a FreeRTOS mutex `m`. L takes `m` and runs a 30 ms critical section. After 10 ms, H wakes and tries to take `m`. M wakes at 15 ms and runs 20 ms of CPU-bound work; M does not need `m`. What is the worst-case time from H's wake to H's acquisition of `m`?

a) 20 ms — M preempts L and runs to completion before L can release.
b) 35 ms — L's residual critical section + M's full run.
c) 20 ms — L's residual critical section under priority inheritance; M cannot preempt the promoted L.
d) 0 ms — H preempts L immediately because H has higher priority.

---

## 6.

Two tasks A and B each take two FreeRTOS mutexes `m_alpha` and `m_beta`. A takes `m_alpha` then `m_beta`; B takes `m_beta` then `m_alpha`. The system runs for an hour without deadlock. Why?

a) The kernel detects the cycle and breaks it automatically.
b) Priority inheritance prevents the deadlock by promoting whichever task is waiting.
c) The deadlock requires a specific interleaving that did not happen in this run; the bug is still present and a deadlock is statistically likely over longer runs.
d) FreeRTOS mutexes are re-entrant on the same task, so the second take is a no-op.

---

## 7.

You are routing a sensor's `INT` pin to `GP22` on the RP2040 and configuring `GPIO_IRQ_EDGE_RISE`. The shared GPIO ISR is dispatched from `IO_IRQ_BANK0`. Where does the SDK code that identifies *which* GPIO fired live, and how does it work?

a) In `hardware/irq.c`, by reading `NVIC_ISPR0`.
b) In `hardware/gpio.c`, by reading `IO_BANK0.PROC0_INTS0..3` and locating the set nibble for the firing GPIO.
c) In FreeRTOS's port layer, by examining the saved register file of the interrupted task.
d) In the user callback — the SDK passes the raw `IO_BANK0` status as the `events` argument.

---

## 8.

A direct-to-task notification used in `eSetBits` mode lets the producer set bits in the target task's 32-bit notification value. The target task reads the bits with `xTaskNotifyWait(0, 0xFFFFFFFF, &value, timeout)`. What does the second argument (`ulBitsToClearOnExit`) do, and why is `0xFFFFFFFF` the right value here?

a) It clears the bits on entry to the function; `0xFFFFFFFF` clears all bits so the task does not see leftover bits from a previous wake. (Wrong argument position.)
b) It clears the bits on exit; `0xFFFFFFFF` ensures the bits are cleared after the task processes them so the next `xTaskNotifyWait` starts from zero.
c) It is a mask of bits the task is interested in; `0xFFFFFFFF` means "any bit".
d) It is the timeout multiplier; `0xFFFFFFFF` means "forever".

---

## 9.

The gatekeeper pattern eliminates the priority-inversion concern by serializing access to a resource through a single task's queue. What advantage does it have over a mutex with priority inheritance when both patterns are correctly implemented?

a) The gatekeeper has lower latency on every access.
b) The gatekeeper makes the access-pattern code review-able without reasoning about mutex hold times, and removes the possibility of a non-gatekeeper task forgetting to give the mutex back.
c) The gatekeeper uses less RAM because it does not need a mutex control block.
d) The gatekeeper is required by MISRA-C; the mutex pattern is not.

---

## 10.

You enable `__WFI` in `vApplicationIdleHook` and re-flash. The Pico's 3V3 current drops from ~85 mA to ~58 mA. What did you trade off, in terms of worst-case interrupt latency?

a) Nothing — `WFI` resumes on the next clock cycle after an interrupt edge.
b) 1–3 cycles of wake-up latency; invisible to a 200 Hz sensor but measurable on a 100 kHz control loop.
c) ~100 cycles of wake-up latency; visible on every ISR.
d) `__WFI` disables the NVIC entirely; no interrupts fire while WFI is active.

---

## Answer key

1. **(d)**. The kernel does not preempt an ISR. The blocked task becomes ready inside `xStreamBufferSendFromISR`, which sets `xHigher = pdTRUE`. You must call `portYIELD_FROM_ISR(xHigher)` to request a `PendSV` context switch on the way out. Without it, the wake takes effect at the next `SysTick`.

2. **(c)**. On the Pico SDK port the default is `0xC0` (priority 3, lowest). Every SDK-installed interrupt is at this priority unless overridden via `irq_set_priority`. The kernel masks interrupts at priorities numerically *greater than or equal to* this value during critical sections; on the M0+ that means it masks 0xC0 (i.e. everything). Interrupts at numerically lower values (0x00, 0x40, 0x80 — none by default) would remain enabled but could not call any FreeRTOS API.

3. **(d)**. For a fixed 4-byte payload with one ISR producer and one task consumer, a direct-to-task notification with `eIncrement` (or `eSetValueWithOverwrite` if you want to carry the value) is the fastest. ~0.4 µs per signal, no allocation, no critical section. A stream buffer at trigger level 4 would also work and is also fast (~0.6 µs); the notification is marginally faster.

4. **(b)**. The head and tail are owned by separate threads (producer and consumer respectively). The producer reads tail to check for free space; the consumer reads head to check for available bytes. A memory barrier between the data write and the index update ensures the consumer sees the data before it sees the updated head index. No critical section is needed. This is a textbook SPSC ring; FreeRTOS does not invent it. (a) is false — stream buffers do modify kernel data. (c) is false — Cortex-M0+ does not have native CAS. (d) is false — interrupts fire freely during a stream-buffer operation.

5. **(c)**. Priority inheritance promotes L to priority 3 when H tries to take `m`. M (priority 2) cannot preempt the promoted L. L finishes its critical section, releases `m`, drops back to 1. H takes `m`. Wait time = L's residual critical section = 30 − 10 = 20 ms.

6. **(c)**. The deadlock is a latent bug. It requires the specific interleaving where A takes alpha, gets preempted, B takes beta, then both try the second take. Over a long enough run the interleaving will happen. The "passed for an hour" outcome is luck, not correctness. A fix (global acquisition order) is required.

7. **(b)**. The SDK's `gpio_default_irq_handler` in `pico-sdk/src/rp2_common/hardware_gpio/gpio.c` reads the four `IO_BANK0.PROC0_INTS0..3` registers; each is 32 bits divided into 8 nibbles, one per GPIO, with each nibble's four bits indicating LEVEL_LOW, LEVEL_HIGH, EDGE_FALL, EDGE_RISE. The dispatcher loops over the four registers, finds the set nibble(s), computes the GPIO number and event mask, and calls the user callback.

8. **(b)**. `ulBitsToClearOnExit` is applied *after* the task wakes; setting it to 0xFFFFFFFF clears all bits so the next `xTaskNotifyWait` starts clean. (The first argument `ulBitsToClearOnEntry` is the on-entry clear; 0 here means "don't pre-clear anything".) The bug avoided by `0xFFFFFFFF` exit-clear: forgetting to clear lets old bits persist into the next wait, masking new events.

9. **(b)**. The gatekeeper's main advantages are *analytical*: you reason about the resource access by reading one task's code (the gatekeeper); you cannot forget to release the resource because no other task ever holds it. (a) is false — the gatekeeper adds one context switch per access, so per-access latency is slightly higher. (c) is false — the queue costs more RAM than the mutex. (d) is not a real MISRA rule.

10. **(b)**. The Cortex-M0+ `WFI` instruction halts the CPU clock until any NVIC interrupt becomes pending. The hardware resumes within 1–3 cycles of the interrupt edge (the exact number depends on the wake-up path through the clock domain). For a 200 Hz sensor (5 ms period) the 1–3 cycles is < 0.001 % of the period; for a 100 kHz control loop (10 µs period) it can be ~10 % of the period and is meaningful. (a) is too aggressive; (c) overstates the cost; (d) is wrong — `WFI` does not disable the NVIC.

---

## Self-grade

| Correct | Verdict                                                          |
|--------:|------------------------------------------------------------------|
|     10 | You can lecture next week. Stretch into the homework's stretch problems. |
|      9 | Solid grasp. Move on.                                            |
|      8 | Ready. Skim the lecture you missed the question from.             |
|      7 | Marginal. Re-read Lectures 2 and 3 and re-run Exercise 2.         |
|      6 | Re-read all three lectures.                                      |
|    ≤ 5 | Re-do the week. The mini-project will not work without these fundamentals. |
