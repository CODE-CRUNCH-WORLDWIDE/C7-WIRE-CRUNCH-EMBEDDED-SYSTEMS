# Week 6 — Quiz

Ten questions covering preemption, mutex vs binary semaphore, ISR-safe APIs, stack sizing, the FreeRTOS scheduling model, and the priority-inheritance protocol. Closed-book; the answers are at the bottom but read them only after attempting all ten.

Aim for 8 / 10 to consider yourself ready to start Week 7. If you score below 6, re-read Lecture 3 and re-run Challenge 1.

---

## 1.

Two FreeRTOS tasks A and B are both at priority 2 and both ready to run. The kernel is configured with `configUSE_PREEMPTION = 1` and `configUSE_TIME_SLICING = 1`. Task A is currently running. The `SysTick` ISR fires (tick boundary). What does the scheduler do?

a) Continues running A — same-priority means no switch.
b) Switches to B for one tick, then back to A.
c) Picks either A or B by FIFO order through the priority-2 ready list.
d) Calls `vApplicationTickHook` and stops there.

---

## 2.

You replace the mutex in Exercise 3 with `xSemaphoreCreateBinary` (and an explicit `xSemaphoreGive` at startup to prime it). The two same-priority print tasks now appear to run identically: every line lands whole on the wire. You add a third task at priority 3 that occasionally writes to the UART through the same guard. Under what specific condition will the system now exhibit a bug that the mutex version would have avoided?

a) When the priority-3 task tries to take the guard while a priority-2 task holds it, and a priority-1 task is running CPU-bound work.
b) When the priority-3 task tries to take the guard while a priority-1 task holds it, and a priority-2 task is running CPU-bound work.
c) When all three tasks try to take the guard simultaneously.
d) Never — binary semaphores and mutexes are interchangeable when item-size is zero.

---

## 3.

Inside a GPIO interrupt handler you want to wake a task that is blocked on a queue. Which sequence is correct?

a) `xQueueSend(q, &item, portMAX_DELAY); taskYIELD();`
b) `xQueueSendFromISR(q, &item, NULL); portYIELD_FROM_ISR(pdTRUE);`
c) `BaseType_t w = pdFALSE; xQueueSendFromISR(q, &item, &w); portYIELD_FROM_ISR(w);`
d) `xQueueSend(q, &item, 0); /* no yield needed; ISR returns on its own */`

---

## 4.

`uxTaskGetStackHighWaterMark` returns 12 for a task that was configured with a 256-word stack. Interpreting the value:

a) The task has used 12 words of stack since creation.
b) The task has 12 words of stack remaining unused as the minimum free-space watermark.
c) The task's stack is 12 words deep.
d) The task overflowed 12 words past its limit.

---

## 5.

You configure `configCHECK_FOR_STACK_OVERFLOW = 2`. A task overflows its stack briefly during a deep call chain, then unwinds before the next context switch. Will the kernel detect the overflow?

a) Yes — method 2 writes a known pattern across the stack at creation and checks for pattern corruption near the limit on every context switch.
b) No — method 2 only checks the current SP at context-switch time, and after the unwind the SP is back inside the legal range.
c) Yes, but only on Cortex-M3 and above; the M0+ lacks the MPU needed.
d) Only if the overflow is greater than 16 bytes.

---

## 6.

A task using `vTaskDelay(pdMS_TO_TICKS(100))` for periodic work has been observed to drift by about 4 % over a long run. The fix is to:

a) Increase `configTICK_RATE_HZ` to 10 000.
b) Switch from `vTaskDelay` to `vTaskDelayUntil`, which anchors the wake against the previous wake instead of the current time.
c) Use `taskYIELD()` instead of `vTaskDelay`.
d) Pin the task to a specific core with `vTaskCoreAffinitySet`.

---

## 7.

A queue is created with `xQueueCreate(8, sizeof(Event_t))` where `sizeof(Event_t)` is 16 bytes. The kernel allocates approximately how much memory for this queue?

a) 16 bytes.
b) 128 bytes plus ~80 bytes of queue bookkeeping.
c) 8 bytes plus 16 bytes of bookkeeping.
d) The queue is allocated lazily, so 0 bytes at creation time.

---

## 8.

A high-priority task H blocks on a FreeRTOS mutex held by a low-priority task L. While H is blocked, a medium-priority task M becomes ready. Under FreeRTOS's mutex priority-inheritance protocol, what happens?

a) M preempts L, runs to completion, then L resumes and gives the mutex; H runs last. (Classical priority inversion.)
b) L's priority is temporarily promoted to H's priority while it holds the mutex; M cannot preempt L. L releases the mutex, drops back to its original priority, H takes the mutex.
c) The kernel kills L because it is blocking H, and signals an error.
d) H and L deadlock; the kernel detects the cycle and panics.

---

## 9.

`vApplicationIdleHook()` is enabled and contains exactly one instruction: `__asm volatile("wfi");`. The Cortex-M0+ enters sleep on every idle cycle. What is the worst-case impact on interrupt latency?

a) None — `WFI` is exited as soon as any pending interrupt arrives, just like a normal instruction boundary.
b) A few microseconds of additional latency for the clock domain to come back up after `WFI`.
c) An additional 1 ms — one full tick — because the wake only happens at the next `SysTick`.
d) Unbounded — `WFI` can sleep the CPU indefinitely.

---

## 10.

In a four-task system, the task priorities are: sensor = 4, transform = 3, logger = 2, idle = 0 (implicit). The `xQueueSend` from sensor to transform succeeds and the sensor task immediately returns to a `vTaskDelayUntil`. Which task runs next?

a) The logger, because it has been waiting longest.
b) The transform, because it is now the highest-priority READY task.
c) The idle task, because the sensor just blocked.
d) Whichever task the kernel picks at random — same-priority ties broken arbitrarily.

---

## Answers

1. **(b)** — with time-slicing on, same-priority ready tasks round-robin on each tick. The kernel switches from A to B for one tick.

2. **(b)** — priority inversion requires three priority levels. The priority-3 task blocks on the guard held by a priority-1 task; a priority-2 task that does not need the guard preempts the priority-1 task and starves the priority-3 task. With a mutex, the priority-1 task is promoted to priority 3 for the duration of the hold; with a binary semaphore, it is not. Choice (a) inverts the priority numbering — H must be the *highest*, not held-by-the-highest.

3. **(c)** — the `*FromISR` API is mandatory, the `pxHigherPriorityTaskWoken` out-parameter must be initialized to `pdFALSE` and passed through, and `portYIELD_FROM_ISR` is called at the end with the variable so the scheduler yields only if needed. Choice (b) hardcodes `pdTRUE` which forces a yield always (wasteful but not incorrect); choice (a) calls the task-context API from an ISR (illegal).

4. **(b)** — `uxTaskGetStackHighWaterMark` returns the *minimum free space in words* observed since task creation. A value of 12 means the task came within 48 bytes (12 × 4) of its stack limit. Bad sign — re-size with margin.

5. **(a)** — method 2's pattern-fill catches transient overflows that recover before the next switch. Method 1 ("low water") would miss this case; that is exactly why method 2 exists.

6. **(b)** — `vTaskDelayUntil` anchors against the previous wake, so the work time does not accumulate into the period. This is the standard fix for periodic-task drift.

7. **(b)** — 8 × 16 = 128 bytes of storage, plus the `Queue_t` bookkeeping (~80 bytes on the M0+ port). Queues allocate eagerly on `xQueueCreate`.

8. **(b)** — priority inheritance temporarily promotes L. This is the only behaviour that bounds the worst-case priority inversion to the length of L's critical section. Choice (a) describes the *bug* that priority inheritance fixes.

9. **(b)** — `WFI` exits on any enabled interrupt, but the M0+ takes a few microseconds for the clock domain to ramp back up. Acceptable for most tasks; not for hard-real-time microsecond-scale loops. Choice (c) is wrong: `WFI` does not wait for the next tick, it wakes on *any* interrupt.

10. **(b)** — the scheduler picks the highest-priority READY task. The sensor blocked (now in BLOCKED), so transform (priority 3) is the highest READY. Logger (priority 2) and idle (priority 0) only run when no higher-priority task is ready.
