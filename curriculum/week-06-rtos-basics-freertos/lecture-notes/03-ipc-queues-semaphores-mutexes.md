# Lecture 3 — IPC: queues, semaphores, mutexes, and priority inheritance

> *Three primitives, used correctly, are sufficient for every IPC pattern this course will throw at you. Used incorrectly, they cause priority inversion, missed deadlines, or silent data corruption that you will not reproduce in a debugger.*

## The three primitives

FreeRTOS provides three core inter-task communication primitives, each with an `*FromISR` variant for use inside interrupt handlers:

- **Queues** — FIFO of fixed-size copy-by-value items. The general-purpose data-transfer primitive.
- **Semaphores** — binary or counting; used for signalling events, not for transferring data.
- **Mutexes** — like binary semaphores but with ownership and priority inheritance; used for protecting shared resources.

Internally all three share a single C struct (`Queue_t` in `queue.c`) and most of the code path. A queue with item size zero and length one *is* a binary semaphore. A queue with item size zero, length N, and a counting-up sender is a counting semaphore. A queue with item size zero, length one, and the additional priority-inheritance bookkeeping is a mutex. The differences are in the bookkeeping, not the underlying machinery.

That said, the API distinguishes them clearly and you should use the right call for the intent: `xSemaphoreTake` for signalling, `xQueueReceive` for data transfer, even when the underlying machinery is the same. The code reads correctly when the API matches the intent.

## Queues

A queue is created with a length and an item size:

```c
QueueHandle_t xQueue = xQueueCreate(4, sizeof(SensorReading_t));
configASSERT(xQueue != NULL);
```

Length 4 = the queue can hold up to four pending items. Item size = the kernel `memcpy`s `sizeof(SensorReading_t)` bytes per send and per receive. The kernel allocates `length * item_size` bytes for the storage plus ~80 bytes for the queue's bookkeeping (the `Queue_t` struct).

To send:

```c
SensorReading_t reading = { .raw = 0x4321, .timestamp = xTaskGetTickCount() };
if (xQueueSend(xQueue, &reading, pdMS_TO_TICKS(10)) != pdTRUE) {
    /* timeout — queue was full for 10 ms; sensor producer is faster than
     * consumer or the consumer is blocked. Decide: drop, log, or escalate. */
}
```

The first argument is the queue. The second is a pointer to the item *to be copied in* — the kernel `memcpy`s `sizeof(SensorReading_t)` bytes from the pointer into the queue's storage. After the call returns, the caller's `reading` variable can be modified or go out of scope; the kernel owns its copy. The third argument is the maximum block time: 0 means "don't block, return `pdFALSE` immediately if full", `portMAX_DELAY` means "block forever", anything else means "block up to this many ticks".

To receive:

```c
SensorReading_t reading;
if (xQueueReceive(xQueue, &reading, portMAX_DELAY) == pdTRUE) {
    handle_reading(&reading);
}
```

Symmetric. The kernel `memcpy`s from the queue's storage into the caller's pointer. Block time 0 = peek-and-go, `portMAX_DELAY` = block until something arrives.

A queue full and a queue empty are the two conditions a task may block on. The kernel maintains per-queue *waiters* lists; a task that blocks on a full queue is moved to that queue's "send-waiters" list, a task that blocks on an empty queue is moved to the "receive-waiters" list. When the queue state changes (a send into a previously-empty queue, a receive from a previously-full queue), the kernel checks the appropriate waiters list and unblocks the highest-priority waiter.

### `xQueueSendFromISR` and the yield dance

From an ISR you must use `xQueueSendFromISR`:

```c
void __isr_handler_GP15_falling_edge(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    ButtonEvent_t evt = { .ticks = xTaskGetTickCountFromISR() };

    /* the ISR-safe send. Does not block; returns errQUEUE_FULL if full. */
    xQueueSendFromISR(xButtonQueue, &evt, &xHigherPriorityTaskWoken);

    /* if the send unblocked a task that has higher priority than the
     * current (interrupted) task, request a context switch on the way out
     * of this ISR — otherwise the wake takes effect on the next tick,
     * which could be up to 1 ms later. */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

The `BaseType_t xHigherPriorityTaskWoken` out-parameter is the kernel's way of telling the ISR "you just unblocked a task that has higher priority than the one you interrupted; please yield on the way out". The ISR is required to honour the request — `portYIELD_FROM_ISR` is the macro that requests the `PendSV` exception, which performs the context switch on the return from the ISR.

If you forget the `portYIELD_FROM_ISR`, the system still works correctly — it just has worse latency. The unblocked task waits for the next tick (up to 1 ms later) before being dispatched. For a 100 Hz button interrupt this might be invisible. For a 10 kHz ADC event this might be your entire response budget.

### Why use a queue for ISR-to-task hand-off

The alternative — having the ISR call the task's code directly — is wrong for three reasons:

1. **The task may be in a critical section.** An ISR can run at any time. The task it interrupts may have a lock held, may be halfway through a shared-data update, or may have left an invariant temporarily broken. Calling task code from the ISR violates the assumed serialization.
2. **The ISR-context API surface is tiny.** Most peripherals' `*_blocking` driver calls are illegal in ISR context. If the ISR needs to do nontrivial work, it must defer to a task that runs in task context. The queue is the deferral mechanism.
3. **Backpressure.** A queue with a finite length provides natural backpressure: if events come faster than the task can consume, the queue fills, `xQueueSendFromISR` returns `errQUEUE_FULL`, and the ISR has a choice — drop, count-and-log, or escalate. Direct-call has no backpressure; the ISR just preempts the task more often, and the system degrades opaquely.

### `xQueueOverwrite` — the overwrite-on-full variant

For a single-element queue (`xQueueCreate(1, ...)`) FreeRTOS offers `xQueueOverwrite`, which always succeeds and replaces the existing item if any. Use case: a "latest value" mailbox where the consumer always wants the most recent reading and historical readings have no value. We use this in Challenge 2.

```c
QueueHandle_t xLatestReading = xQueueCreate(1, sizeof(SensorReading_t));
/* producer side */
xQueueOverwrite(xLatestReading, &fresh_reading);  /* never blocks, never fails */
/* consumer side */
xQueuePeek(xLatestReading, &reading_copy, 0);  /* read without removing */
```

`xQueueOverwrite` writes the new value, discarding the old one if present. `xQueuePeek` reads the current value without consuming it. Together they form a "latest-value mailbox" pattern.

## Semaphores: binary and counting

### Binary semaphore

A binary semaphore has a single bit of state — "given" or "not given". `xSemaphoreGive` raises it. `xSemaphoreTake` lowers it (blocking until it is raised).

```c
SemaphoreHandle_t xButtonPressed = xSemaphoreCreateBinary();
configASSERT(xButtonPressed != NULL);

/* the ISR side: a button press gives the semaphore */
void __isr_handler_GP15_falling_edge(void)
{
    BaseType_t xWoken = pdFALSE;
    xSemaphoreGiveFromISR(xButtonPressed, &xWoken);
    portYIELD_FROM_ISR(xWoken);
}

/* the task side: blocks until a press arrives */
static void vButtonTask(void *p)
{
    for (;;) {
        if (xSemaphoreTake(xButtonPressed, portMAX_DELAY) == pdTRUE) {
            handle_button_press();
        }
    }
}
```

This is the "signal one event happened, do not preserve count" pattern. If the ISR fires three times before the task gets to run, the task wakes once and sees one event — the second and third "give" calls find the semaphore already up and are no-ops. This is fine for events where "≥ 1 happened since last check" is the question; it is wrong if the count matters.

### Counting semaphore

A counting semaphore preserves the count. Created with a max and an initial value:

```c
SemaphoreHandle_t xEventCounter = xSemaphoreCreateCounting(
    UINT32_MAX,  /* max count */
    0            /* initial count */
);
```

Each `xSemaphoreGive` increments (saturating at max), each `xSemaphoreTake` decrements (blocking when zero). Use case: count the number of available buffers in a pool, or the number of events queued since last service. The task can wake and process every event one by one until the count is zero, never losing one.

### Binary vs counting: choosing

If the task should react to the *occurrence* of events but does not care how many happened: binary. If the task should process *every* event individually: counting (or a queue carrying the event payload — usually a better choice, because the payload carries the data the task needs to process).

The third option, and often the right one, is **task notifications** — a per-task 32-bit value updated by `xTaskNotify` / `vTaskNotifyGive` from another task or ISR, and consumed by `ulTaskNotifyTake` / `xTaskNotifyWait` from the task. About 4× faster than a binary semaphore (no queue allocation, no queue lookup), and supports both binary and counting modes. The catch: only one task can be notified per call, so it is strictly point-to-point. Out of scope for this lecture but worth knowing exists.

## Mutexes

A *mutex* (mutual-exclusion lock) protects a shared resource. Exactly one task holds the mutex at a time; other tasks wanting to enter the critical section block on `xSemaphoreTake` until the current holder calls `xSemaphoreGive`.

```c
SemaphoreHandle_t xUartMutex = xSemaphoreCreateMutex();
configASSERT(xUartMutex != NULL);

static void vLogger(const char *msg)
{
    /* try to acquire the mutex; block up to 100 ms */
    if (xSemaphoreTake(xUartMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uart_puts(uart0, msg);
        xSemaphoreGive(xUartMutex);
    } else {
        /* could not acquire — UART contended for > 100 ms. Drop or count. */
    }
}
```

Looks like a binary semaphore. Behaves differently in two important ways:

**Ownership.** Only the task that took the mutex may give it back. If task A takes and task B gives, FreeRTOS will `configASSERT` (in development) or silently corrupt its bookkeeping (in production with assertions disabled). Use a binary semaphore for any pattern where the giver is not the taker — for example, an ISR signalling a task. Use a mutex *only* for "this task owns the resource for the duration of the critical section, then releases".

**Priority inheritance.** This is the load-bearing feature.

## Priority inheritance

The priority-inversion problem: a high-priority task H tries to take a mutex held by a low-priority task L. H is now blocked waiting on L. A medium-priority task M (which does not need the mutex) becomes ready and preempts L. M runs to completion. L cannot run because M is higher priority. H cannot run because L still holds the mutex. The system is now scheduling M *instead of* H, even though H has the highest priority of the three. H is *inverted* — its effective priority has dropped to L's.

If M is short-running, the inversion lasts only until M finishes and L can resume; then L releases the mutex, H takes it, and the system recovers. If M is long-running or periodic, H can be starved indefinitely. The Mars Pathfinder reboot loop (1997) was exactly this: a low-priority meteorology task held a VxWorks pipe mutex, a medium-priority comms task preempted it for tens of milliseconds at a time, and the high-priority bus-management task missed its deadline. The watchdog rebooted the lander every few hours until the team uplinked a patch enabling priority inheritance on the mutex.

**The priority-inheritance protocol** (Sha, Rajkumar, Lehoczky 1990): when task H blocks on a mutex held by L, *temporarily* promote L to H's priority for the duration of the mutex hold. M can no longer preempt L because L is now running at H's priority. L finishes its critical section, releases the mutex, drops back to its original priority. H takes the mutex and runs.

The bound: the worst-case priority inversion under priority inheritance is *the length of the longest critical section L might be in*. This is a finite, knowable number — you can audit every mutex hold in your codebase and write the maximum hold time down. Without priority inheritance, the bound is *the worst-case run time of every higher-priority-than-L-but-lower-priority-than-H task*, which is generally not knowable.

FreeRTOS mutexes (`xSemaphoreCreateMutex`) implement priority inheritance automatically. Binary semaphores (`xSemaphoreCreateBinary`) do not. This is the only reason to use a mutex instead of a binary semaphore — and it is a sufficient reason whenever the protected entity is a resource (UART, I²C bus, shared data structure) rather than an event.

**Mutex vs binary semaphore — the one-line rule.** *Use a mutex when the same task takes and gives, and what is being protected is a resource. Use a binary semaphore when the giver and the taker are different actors, and what is being communicated is an event.*

Memorize this. The quiz will ask.

### What priority inheritance does NOT solve

Priority inheritance bounds *priority inversion*, not other pathologies:

- **Deadlock.** If task A takes mutex X and then tries to take mutex Y, and task B takes Y and then tries to take X, both block forever. Priority inheritance does not help — neither task is preempting the other; they are both blocked. Fix: define a lock-order convention (always take X before Y) and audit every call site.
- **Convoy.** If many tasks contend for the same mutex, they serialize at the mutex. Priority inheritance does not solve this — it makes the system *correct* but does not make it *fast*. Fix: shorter critical sections, finer-grained mutexes, or a different IPC pattern (a queue that decouples producer and consumer entirely).
- **Lock recursion.** FreeRTOS mutexes are *not* recursive by default — a task taking the same mutex twice deadlocks against itself. Use `xSemaphoreCreateRecursiveMutex` and `xSemaphoreTakeRecursive` / `xSemaphoreGiveRecursive` if you need recursion. Read the API docs carefully: every recursive take needs a matching recursive give before the mutex is actually released.

## The ISR-safe API surface

Every kernel function that can be called from an interrupt handler has a `*FromISR` variant. The taxonomy:

| Task-context API           | ISR-context API                          |
|----------------------------|------------------------------------------|
| `xQueueSend`               | `xQueueSendFromISR`                      |
| `xQueueReceive`            | `xQueueReceiveFromISR`                   |
| `xQueueOverwrite`          | `xQueueOverwriteFromISR`                 |
| `xQueuePeek`               | `xQueuePeekFromISR`                      |
| `xSemaphoreTake`           | `xSemaphoreTakeFromISR`                  |
| `xSemaphoreGive`           | `xSemaphoreGiveFromISR`                  |
| `xTaskNotify`              | `xTaskNotifyFromISR`                     |
| `vTaskNotifyGive`          | `vTaskNotifyGiveFromISR`                 |
| `xTaskGetTickCount`        | `xTaskGetTickCountFromISR`               |
| `vTaskDelay`               | (illegal — ISRs do not block)             |
| `xSemaphoreTake` (timeout) | `xSemaphoreTakeFromISR` (poll only)      |
| `vPortYield`               | `portYIELD_FROM_ISR(xWoken)`             |

Rules:

1. From an ISR, *only* the `*FromISR` API is legal.
2. The `*FromISR` API never blocks. A `Take` from an ISR returns "got it" or "did not get it"; it does not wait.
3. Every `*FromISR` API that can wake a task takes a `BaseType_t *pxHigherPriorityTaskWoken` out-parameter. The ISR must initialize it to `pdFALSE`, pass it to the API call, and on the way out call `portYIELD_FROM_ISR(xHigherPriorityTaskWoken)` to request a context switch if needed.

The compiler does not enforce these rules. The runtime catches some violations with `configASSERT` (typically when `assert` is enabled in `FreeRTOSConfig.h`). Others — calling a task-context API from an ISR when assertions are disabled — silently corrupt the scheduler. Read the API docs and double-check every `*` in the function name.

### `configASSERT` in development

Enable `configASSERT( x )` in `FreeRTOSConfig.h`:

```c
/* in FreeRTOSConfig.h */
#include "pico/stdlib.h"
extern void vAssertCalled(const char *file, int line);
#define configASSERT( x ) if ((x) == 0) vAssertCalled(__FILE__, __LINE__)
```

And the function (in your `main.c` or a separate `assert.c`):

```c
void vAssertCalled(const char *file, int line)
{
    /* Print the location to a known UART (carefully — the UART task may be
     * deadlocked) and halt. */
    taskDISABLE_INTERRUPTS();
    /* Either: panic and reset, or: spin so the debugger can attach. */
    for (;;) {
        /* If a debugger is attached you can read `file` and `line` from
         * the stack frame. The watchdog (if enabled) will reboot us if not. */
    }
}
```

The assertions cost a few hundred bytes of `.text` plus a handful of cycles per kernel call. They catch most of the wrong-API-from-ISR class of bugs. Ship with them enabled if you can afford it; otherwise disable them only after the firmware has been through stress testing in dev.

## Critical sections

Sometimes you need to access a shared variable from both a task and an ISR, and a queue or mutex is the wrong tool (too heavy, or the access is too brief to justify the API call). The escape hatch is a *critical section* — a block during which interrupts are disabled or only a subset are allowed:

```c
taskENTER_CRITICAL();
/* read or update shared state; the kernel cannot preempt us */
shared_counter++;
taskEXIT_CRITICAL();
```

These macros expand to `__disable_irq()` / `__enable_irq()` on Cortex-M0+ (with bookkeeping to handle nesting). The cost: while the critical section is active, *no* interrupt fires — not even ones unrelated to the data being protected. This trades correctness against responsiveness; a long critical section delays every other interrupt. Keep critical sections to a few instructions.

A subtlety: in an ISR, the equivalent is `portSET_INTERRUPT_MASK_FROM_ISR` / `portCLEAR_INTERRUPT_MASK_FROM_ISR` — the task macros assume task context and will misbehave if invoked from an ISR. Distinct macro for distinct context.

For data shared between *two tasks* and *not an ISR*, the right tool is a mutex (or a queue). Critical sections are the right answer only when an ISR is one of the participants and the access is too brief for a `*FromISR` API call.

## A worked pattern: gatekeeper task

The most useful pattern this week is the *gatekeeper*: a single task owns a resource, and all other tasks send requests to it via a queue. No mutex needed — the resource is single-owner by construction. We use this for the UART in the mini-project's stretch goal: instead of every task taking a UART mutex, the UART is owned by a dedicated `vUartTask`, every other task posts an output string into a queue, and the UART task drains the queue and writes to the wire.

```c
static QueueHandle_t xUartOutQueue;

static void vUartTask(void *p)
{
    char line[80];
    for (;;) {
        if (xQueueReceive(xUartOutQueue, line, portMAX_DELAY) == pdTRUE) {
            uart_puts(uart0, line);
        }
    }
}

void log_line(const char *msg)
{
    char buf[80];
    snprintf(buf, sizeof(buf), "[%lu] %s\r\n",
             (unsigned long)xTaskGetTickCount(), msg);
    /* fire-and-forget; if the queue is full, drop. */
    xQueueSend(xUartOutQueue, buf, 0);
}
```

Two advantages: (a) no mutex contention because there is no mutex; (b) the system has natural backpressure — if a task generates lines faster than the UART can drain them, the queue fills and the producer is told. The disadvantage is the per-message memcpy overhead (80 bytes copied per call); for high-throughput logging a ring buffer is more appropriate.

Exercise 3 uses a mutex; the mini-project stretch goal hints at the gatekeeper as an alternative. Both are valid; the choice is about whether you want explicit serialization (mutex) or queue-based decoupling (gatekeeper).

## A worked anti-pattern: protecting data with a binary semaphore

Do not do this:

```c
/* WRONG — using a binary semaphore where a mutex is required */
SemaphoreHandle_t xBusGuard = xSemaphoreCreateBinary();
xSemaphoreGive(xBusGuard);  /* prime it so first take succeeds */

/* in any task */
xSemaphoreTake(xBusGuard, portMAX_DELAY);
i2c_write_blocking(i2c0, addr, buf, len, false);
xSemaphoreGive(xBusGuard);
```

The code looks correct but it has the priority-inversion bug. A high-priority task blocked on `xBusGuard` while a low-priority task holds it will be starved by any medium-priority task that becomes ready. The fix is two characters:

```c
SemaphoreHandle_t xBusGuard = xSemaphoreCreateMutex();
/* no priming — mutexes start in the "given" state */
```

Same API for take/give; different bookkeeping inside the kernel. Priority inheritance applied automatically. The compiler does not warn you. The quiz will ask why.

## What's next

Three exercises walk these primitives in turn: two-task scheduling (no IPC), button-to-LED via a queue, UART-shared-via-mutex. Two challenges push the boundary: a priority-inversion demo that you fix by switching binary-semaphore → mutex, and an `xQueueOverwrite`-based bounded mailbox. The mini-project assembles a three-task sensor pipeline.

By Sunday: you can articulate, in one sentence each, when to use a queue, when to use a binary semaphore, when to use a mutex, and what priority inheritance does. Those four sentences are the practical core of Week 6.

## Further reading

- "Mastering the FreeRTOS Real Time Kernel" Chapter 4 (Queue Management) and Chapter 7 (Resource Management). Read Chapter 7 carefully — the worked priority-inversion example in §7.5 is the basis for Challenge 1.
- FreeRTOS API reference for `xQueueCreate`, `xQueueSend`, `xQueueReceive`, `xSemaphoreCreateBinary`, `xSemaphoreCreateCounting`, `xSemaphoreCreateMutex`. Reference cards:
  <https://www.freertos.org/a00018.html> and <https://www.freertos.org/a00113.html>
- Sha, Rajkumar, Lehoczky, "Priority Inheritance Protocols", IEEE Trans. Computers 39(9), Sep 1990. The original paper. Read §III (the worked example) at minimum:
  <https://www.cs.cmu.edu/~rajkumar/papers/article_priority_inheritance.pdf>
- Mike Jones, "What really happened on Mars Rover Pathfinder", 1997. Four-page case study; the canonical real-world priority-inversion incident:
  <https://www.cs.unc.edu/~anderson/teach/comp790/papers/mars_pathfinder_long_version.html>
