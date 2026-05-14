# Lecture 2 — Tasks, stacks, and the scheduler

> *Every task is a function plus a stack plus a control block. The kernel does not allocate any of those for you implicitly — you size them, you name them, you reason about them. The kernel only keeps the bookkeeping.*

## Anatomy of a task

A FreeRTOS task is three things: a function with the signature `void f(void *param)`, a stack you supply (the kernel allocates it from the heap or you allocate it statically), and a Task Control Block (TCB) the kernel maintains. The function never returns. The stack is the task's private workspace. The TCB is the kernel's bookkeeping.

The canonical task body:

```c
static void vSensorTask(void *pvParameters)
{
    const TickType_t xPeriod = pdMS_TO_TICKS(100);
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        /* do work */
        read_sensor_and_post_to_queue();

        /* sleep until xLastWakeTime + xPeriod, accumulating with the previous wake
         * so we are precisely periodic and not just "100 ms-ish" */
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}
```

A task body is an infinite loop. There is no "main returns and the task exits" — if the function returns, the kernel calls `configASSERT(0)` (in development) or jumps to a fault handler. To exit a task cleanly, call `vTaskDelete(NULL)` from inside it; the kernel will deschedule it and the idle task will eventually free its TCB and stack.

`xTaskCreate` builds the task:

```c
TaskHandle_t xSensorTaskHandle = NULL;
BaseType_t rc = xTaskCreate(
    vSensorTask,        /* function pointer */
    "sensor",           /* human-readable name, copied into the TCB */
    512,                /* stack depth in WORDS (so 512 words = 2 KB on M0+) */
    NULL,               /* pvParameters — opaque pointer passed to the task */
    3,                  /* uxPriority */
    &xSensorTaskHandle  /* out: handle to use for vTaskDelete, vTaskSuspend, etc. */
);
configASSERT(rc == pdPASS);
```

The five things you specify, in order: what the task does, what to call it, how big a stack to give it, what argument to pass it, what priority to schedule it at. The kernel asks you all five questions explicitly. There are no defaults.

## The stack

Cortex-M0+ stacks grow *downward*. When `xTaskCreate` runs, the kernel asks the configured allocator (typically `heap_4.c`) for `stack_depth * sizeof(StackType_t)` bytes — on the M0+ port `StackType_t` is `uint32_t`, so 512 stack words = 2 048 bytes — and arranges the stack such that:

1. The very top of the allocated region is set as the *initial stack pointer*.
2. The top 16 words are pre-loaded with a *fake exception frame*: a fake `R0`–`R3`, `R12`, `LR`, `PC = address-of-vSensorTask`, `xPSR = 0x01000000` (Thumb bit set). On Cortex-M (every variant) plus the additional 8 words for `R4`–`R11` that the port adds.
3. The TCB's `pxTopOfStack` field is set to point just below the fake frame.

This is the trick that makes the first context-switch work. When the scheduler eventually picks this task and the `PendSV` handler runs the standard "pop registers from the stack" sequence, it pops the fake frame as if the task had previously been preempted at the entry to `vSensorTask`. The CPU loads PC with the address of `vSensorTask` and the task starts running.

This means the *minimum* useful stack depth is approximately 16 (the saved register set) + the deepest call chain inside the task + the deepest call chain inside any ISR (because ISRs use the task's stack on Cortex-M0+ — there is no separate interrupt stack the way there is on Cortex-A). For a task that calls only itself and uses a few local variables, 128 words (512 bytes) is usually enough. For a task that calls `printf` from `pico-sdk`, you need 1 024 words (4 KB) — `printf` is variadic, recursive (during float formatting), and uses around 800 words of stack on the SDK's snprintf implementation. The exercise solutions document the budgets.

## Stack sizing by measurement

Do not guess. Measure.

The FreeRTOS API call is `uxTaskGetStackHighWaterMark(TaskHandle_t xTask)`:

> Returns the high water mark of the stack associated with `xTask`. That is, the minimum free stack space there has been (in words, so on a 32-bit architecture a value of 1 means 4 bytes) since the task started.

The recipe:

1. Set every task's stack to 1 024 words at first.
2. Run the firmware through every code path you care about — idle, peak sensor read, error-log dump, the long path through the print task.
3. Periodically (every 10 seconds or so) call `uxTaskGetStackHighWaterMark` for every task and print the result over UART.
4. Take the *peak* watermark observed across all runs and across all conditions. Subtract from the configured stack depth to get peak *usage*. Round up to the next 64-word boundary. Add a 64-word margin. That is your shipping stack size.

Example, from a typical mini-project run:

```
sensor    HWM = 384 words free   (peak usage = 640 words)
transform HWM = 720 words free   (peak usage = 304 words)
uart      HWM = 192 words free   (peak usage = 832 words)  <-- printf-heavy
idle      HWM = 124 words free   (peak usage = 132 words)
```

Final shipping budget:

| Task      | Configured | Peak usage | Margin (words) |
|-----------|-----------:|-----------:|---------------:|
| sensor    |        768 |        640 |            128 |
| transform |        384 |        304 |             80 |
| uart      |       1024 |        832 |            192 |
| idle      |        256 |        132 |            124 |

The unallocated room (`configured - peak`) is your safety margin for a code path you did not exercise during testing. Below 64 words on Cortex-M0+ is dangerous — one ISR with a non-trivial call chain can blow the stack. Above 256 words is wasteful — RAM is finite (264 KB on the RP2040, but ~200 KB after the kernel and the stdio buffers).

## Stack-overflow detection

The Cortex-M0+ has no hardware stack-limit register. If you exceed the stack you have allocated, the next push wanders into the next task's TCB (memory layout permitting) or into a neighbouring buffer. The corruption is silent. The fault — a wild branch on a corrupted return address, a wild write through a corrupted pointer — surfaces seconds or minutes later, in code that has nothing to do with the originating task.

FreeRTOS provides two software checks, selected by `configCHECK_FOR_STACK_OVERFLOW`:

- **Method 1 ("low water").** On every context switch, the kernel compares the task's current stack pointer against the start of its allocated region. If SP has dropped below the start, the kernel calls `vApplicationStackOverflowHook(xTask, pcTaskName)`. Catches the overflow on the *first* context switch after the overflow happened — but if the task overflows briefly and then unwinds before the next switch, the check misses it.
- **Method 2 ("high water").** Method 1 *plus* a known pattern (`0xA5A5A5A5`) written across the entire stack at task-creation time. On every context switch, the kernel additionally examines the last 16 bytes of the stack region (near the end the SP would hit at full depth). If any of those bytes is not `0xA5`, the overflow happened at some point during the task's execution even if the SP has since recovered. Catches more, including overflows that came-and-went between context switches. About 2 % runtime overhead.

Use Method 2 in development:

```c
/* in FreeRTOSConfig.h */
#define configCHECK_FOR_STACK_OVERFLOW    2
```

And supply the hook:

```c
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    /* The task named pcTaskName overflowed its stack.
     * At this point the system is in an undefined state — the only safe
     * action is to halt and rely on the watchdog (or your debugger) to
     * recover. Do NOT try to printf — the UART task may itself be the
     * one that overflowed. */
    (void)xTask;
    /* Make the failure observable: GPIO toggle, blink the on-board LED, halt. */
    for (;;) {
        /* spin */
    }
}
```

In a shipping build you may keep `configCHECK_FOR_STACK_OVERFLOW = 2` (the overhead is tolerable) or downgrade to `= 1`. Disabling the check entirely is rarely the right call — the savings are negligible and the diagnostic value is high.

## The idle task and the idle hook

When no other task is ready, the scheduler runs the idle task. The kernel creates it automatically — you do not call `xTaskCreate` for it. The idle task is always at priority 0 (lowest possible), has a stack of `configMINIMAL_STACK_SIZE` words (default 128 on the SDK port), and runs at most one tick at a time before the scheduler re-checks the ready list.

Two reasons the idle task exists: (a) the scheduler always needs a task to run; (b) the idle task is the one that reclaims the TCBs and stacks of tasks that have been deleted via `vTaskDelete`. If you never delete tasks, the idle task is otherwise a no-op spinner.

You can supply `vApplicationIdleHook()` (enabled with `configUSE_IDLE_HOOK = 1`) and the kernel calls it every iteration of the idle loop. Common uses:

- **Power saving.** Call `__WFI` (Wait For Interrupt) inside the hook. The Cortex-M0+ stops the clock to the core until the next interrupt fires. On the RP2040 this drops the system from ~20 mA to ~2 mA. Tradeoff: latency to the next ISR increases by the wake-up time of the clock domain — typically a few microseconds, fine for most workloads.
- **Watchdog tickle.** If you have enabled the RP2040's watchdog peripheral (`hardware_watchdog`), the idle hook is a reasonable place to feed it. Caveat: if the system stalls in a high-priority task, the idle hook never runs and the watchdog correctly resets the system. That is the desired behaviour.
- **Statistics flush.** Move a CPU-percentage snapshot from a kernel buffer to a UART output queue. Light enough work that idle priority is appropriate.

What *not* to do in the idle hook: anything that blocks. The idle task is special — `vTaskDelay` from the idle task is undefined; `xQueueReceive(q, ..., portMAX_DELAY)` from the idle task hangs the entire system because there is no lower-priority task to run when idle blocks. Keep the hook strictly non-blocking.

A minimum idle hook that just sleeps the CPU until the next tick:

```c
#include "hardware/structs/scb.h"

void vApplicationIdleHook(void)
{
    __asm volatile ("wfi");
}
```

## Periodic vs aperiodic tasks

Most RTOS tasks are *periodic*: they wake at a fixed cadence, do work, and sleep until the next wake. The right API for periodic work is `vTaskDelayUntil`, not `vTaskDelay`:

```c
/* WRONG — drifts: every iteration adds (work-time) to the period */
for (;;) {
    do_work();
    vTaskDelay(pdMS_TO_TICKS(100));  /* sleeps for 100 ms AFTER work finishes */
}

/* RIGHT — does not drift: each wake is anchored at the previous wake + period */
TickType_t xLastWakeTime = xTaskGetTickCount();
const TickType_t xPeriod = pdMS_TO_TICKS(100);
for (;;) {
    do_work();
    vTaskDelayUntil(&xLastWakeTime, xPeriod);  /* wakes at last + 100 ms */
}
```

The difference matters. If `do_work()` takes 4 ms, then under `vTaskDelay(100)` the task runs at 96.15 Hz (1000 / (100 + 4)); under `vTaskDelayUntil` it runs at exactly 10 Hz. The 4 % drift accumulates: over an hour, the `vTaskDelay` task has run ~138 fewer iterations than the `vTaskDelayUntil` task. For a sensor sampling at a known cadence, the drift compounds into time-base error.

`vTaskDelayUntil` does have a failure mode: if `do_work()` takes longer than the period for a few iterations, the function returns immediately (without sleeping) on the catch-up cycles. This is correct behaviour — it preserves the long-term average rate — but it means a misbehaving task can monopolize the CPU. Pair with stack-overflow detection and a watchdog.

For *aperiodic* tasks (a task that wakes when a button is pressed, when a queue has data, when a sensor flags an event) you use the blocking API:

```c
for (;;) {
    /* block here until xQueueSend wakes us up, then loop */
    Event_t evt;
    if (xQueueReceive(queue, &evt, portMAX_DELAY) == pdTRUE) {
        handle_event(&evt);
    }
}
```

`portMAX_DELAY` is the macro that means "block forever". The task uses zero CPU while blocked — it is removed from the ready list entirely. The CPU is free for other tasks.

## What `vTaskDelay` actually does

`vTaskDelay(ticks)` moves the calling task from RUNNING to BLOCKED, records "wake me at `current_tick + ticks`" in a kernel list, and calls the scheduler to pick the next task. The kernel's tick ISR (firing at `configTICK_RATE_HZ`) walks the delayed-task list each tick, moves any task whose wake time has arrived back to READY, and if the wake task is higher priority than the running one, requests a context switch.

The "block list" is a sorted insertion: the new task is placed in the list such that the earliest-waking task is at the head. The tick ISR only has to check the head of the list, not walk the whole list, so the tick is O(1) for "is anyone ready to wake yet?" — important because the tick is on the critical path of every kernel decision.

Consequences worth knowing:

- `vTaskDelay(0)` is equivalent to `taskYIELD()`: the task does not actually delay, but the scheduler re-checks the ready list. If a same-priority task is ready, it runs next. If not, the calling task continues. Useful as a cooperative yield in a tight computational loop.
- `vTaskDelay(1)` on a 1 kHz tick is up to 1 ms — possibly less, because the task is woken on the *next* tick, which could be 1 µs after the call or 999 µs after the call. The expected value is 0.5 ms. If you need precise sub-tick timing, do not use `vTaskDelay` — use a hardware timer or a busy-wait, depending on whether the wait is microseconds (busy) or milliseconds (timer).
- `vTaskDelay(portMAX_DELAY)` is *not* "delay forever" — `portMAX_DELAY` is the largest `TickType_t`, but it is still a finite number. On the M0+ port with `configUSE_16_BIT_TICKS = 0`, `TickType_t` is 32-bit, so `portMAX_DELAY` is about 49.7 days at 1 kHz. To suspend a task indefinitely, use `vTaskSuspend(NULL)`.

## The scheduler in slow motion

The simplest scheduler decision is this: "what is the highest-priority READY task right now? Run it." Implemented in `prvSelectHighestPriorityTask()` in `tasks.c`. On Cortex-M0+ the decision is a count-leading-zeros on a 32-bit bitmap (one bit per priority level, set when at least one task at that priority is ready), followed by a list-walk through the ready list for that priority. Total cost: about 15 cycles for the bitmap lookup plus a few cycles per task in the ready list at that level — usually 1.

The scheduler is invoked from three places:

1. **The tick ISR** (`xPortSysTickHandler`). Fires at `configTICK_RATE_HZ`. Decrements timeouts on blocked tasks, time-slices among same-priority ready tasks if `configUSE_TIME_SLICING`, and requests a context switch if the picked task is different from the current.
2. **Inside a kernel API call** (e.g. `xQueueReceive` when no item is available). The task blocks, the scheduler runs, the next task is picked.
3. **At the end of any ISR that called a `*FromISR` API with a `pxHigherPriorityTaskWoken` out-parameter** that the ISR then passed to `portYIELD_FROM_ISR(xHigherPriorityTaskWoken)`. The macro requests a `PendSV` interrupt, which fires immediately on the return from the originating ISR, performs the context switch, and dispatches the higher-priority task.

The context switch itself is the `PendSV` handler, hand-written in inline assembly in `port.c`. On Cortex-M0+ the routine is:

```
1. Save R4-R11 to the current task's stack (the hardware automatically
   saved R0-R3, R12, LR, PC, xPSR on exception entry).
2. Save the current SP into the current TCB's pxTopOfStack.
3. Call vTaskSwitchContext() (C function) to update the global pxCurrentTCB
   to the new task.
4. Load the new task's pxTopOfStack into the new SP.
5. Pop R4-R11 from the new task's stack.
6. Return from the exception — the hardware pops R0-R3, R12, LR, PC, xPSR
   from the new stack and starts executing the new task.
```

Total: about 84 cycles, plus the cycles for `vTaskSwitchContext` (typically 50–100 more cycles depending on how many priorities are populated). At 125 MHz, the whole switch is under 1.5 µs. On a 1 kHz tick (1 ms tick period), the kernel overhead is about 0.15 % of CPU — invisible.

## Idle hook, tick hook, and the `WFI` trick

Two more hooks worth knowing this week:

- `vApplicationTickHook()` — called from inside the tick ISR, *before* the scheduler runs. Enabled with `configUSE_TICK_HOOK = 1`. Useful for tracking wall-clock time, generating a software watchdog, or sampling a 1 kHz-rate signal. Must be O(1) — it is on the critical path of every tick.
- `vApplicationMallocFailedHook()` — called if `heap_*.c`'s allocator returns NULL. Enabled with `configUSE_MALLOC_FAILED_HOOK = 1`. Useful for forcing an immediate failure rather than silently propagating a NULL up the call stack.

The combination of `vApplicationIdleHook` calling `WFI` plus a 1 kHz tick gives you a passable low-power mode: the CPU sleeps between ticks, wakes for the tick, runs the kernel for ~100 cycles, sleeps again. Power consumption drops 5–10× from the always-on baseline. The latency from interrupt-to-task-wake increases by the few microseconds it takes the clock domain to come back up. Tradeoff documented; choice yours per project.

## Common task-creation mistakes

A list of things you will get wrong at least once. Memorize the symptoms:

1. **Returning from the task function.** Symptom: random crash, often inside `vTaskExitCritical` or `xPortPendSVHandler`. Fix: every task is `for (;;) { ... }`; never let the body fall through. If you need to terminate, call `vTaskDelete(NULL)`.

2. **Stack too small.** Symptom: stack overflow hook fires, with the offending task's name in `pcTaskName`. Or: random corruption far from the task. Fix: measure with `uxTaskGetStackHighWaterMark`, re-size with a 100-word margin. Method-2 stack-overflow detection in dev catches most cases.

3. **Calling a task-context API from an ISR.** Symptom: `configASSERT` fires, or the kernel silently corrupts its ready list. Fix: use the `*FromISR` variant. Compiler does not enforce this — read the API reference.

4. **Forgetting `portYIELD_FROM_ISR` after a `*FromISR` call.** Symptom: ISR woke a higher-priority task but that task does not run until the next tick. Latency is up to 1 ms longer than it should be. Fix: always pass a `BaseType_t` variable to the `pxHigherPriorityTaskWoken` parameter, initialize it to `pdFALSE`, and on the way out call `portYIELD_FROM_ISR(that_variable)`.

5. **Two tasks at the same priority where one never blocks.** Symptom: the other task at that priority never runs. Fix: either make the busy task block periodically (`taskYIELD()` or `vTaskDelay(1)`) or split them across priorities.

6. **Using `vTaskDelay` for a periodic task with non-zero work time.** Symptom: the actual period is `work_time + delay_time`, not `delay_time`. Long-term rate drift. Fix: use `vTaskDelayUntil`.

7. **Calling `printf` from an ISR.** Symptom: the system hangs, deadlocks, or corrupts memory depending on which lock `printf` happens to take. Fix: ISRs put data in a queue; a dedicated logging task drains the queue and calls `printf`. Or skip `printf` entirely from ISRs.

## What's next

Lecture 3 covers the IPC primitives — queues, binary semaphores, counting semaphores, and mutexes — and the priority-inheritance protocol that makes mutexes safe. Exercises 1, 2, and 3 are sequenced to introduce one concept at a time: two tasks (no IPC), then a queue (ISR-to-task), then a mutex (task-to-task resource protection). Challenge 1 makes the priority-inversion failure visible on a logic analyzer.

## Further reading

- "Mastering the FreeRTOS Real Time Kernel" Chapter 3 (Tasks). About 60 pages. Read end-to-end before Exercise 1.
- FreeRTOS API reference for `xTaskCreate`, `vTaskDelay`, `vTaskDelayUntil`, `uxTaskGetStackHighWaterMark`. Reference cards are short:
  <https://www.freertos.org/a00125.html>
- RP2040 datasheet, §2.4 (Cortex-M0+ Processor), pp. 65–73. The hardware FreeRTOS runs on. Read §2.4.5 (Nested Vectored Interrupt Controller) pp. 99–104 in particular — it explains why `PendSV` is at the lowest priority and why that is the right choice.
