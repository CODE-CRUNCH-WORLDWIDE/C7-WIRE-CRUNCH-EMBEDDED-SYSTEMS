# Lecture 2 — Stream Buffers, Message Buffers, and Direct-to-Task Notifications

> *A queue is a box. A stream buffer is a pipe. A notification is a wire. They look interchangeable on a slide and they are not: each has a different concurrency contract, a different memory cost, and a different latency. Picking the right one for an ISR handoff is the single most consequential decision in an event-driven firmware.*

## The four primitives, ranked by weight

This lecture lays out the four FreeRTOS data-passing primitives you can use to hand off from an ISR (or from one task) to another task. We rank them by code size, memory footprint, and per-operation latency on Cortex-M0+ at 125 MHz with `configMAX_PRIORITIES = 8` and the Pico SDK port defaults.

| Primitive                       | Item shape       | Multi-prod / multi-cons    | Memory per object (12-byte item, length 4) | `*FromISR` latency on M0+ |
|---------------------------------|------------------|----------------------------|-------------------------------------------:|--------------------------:|
| Queue (`xQueueCreate`)          | fixed-size items | yes / yes                  |                                       80 B |                     1.4 µs |
| Counting semaphore              | a `UBaseType_t` count | yes / yes              |                                       72 B |                     0.9 µs |
| Binary semaphore                | one bit          | yes / yes                  |                                       72 B |                     0.8 µs |
| Stream buffer                   | byte stream      | **one / one** (SPSC)       |                                       64 B |                     0.6 µs |
| Message buffer                  | byte stream + length prefix | **one / one** (SPSC) |                                       64 B |                     0.7 µs |
| Direct-to-task notification     | 32-bit value or bit-mask | one ISR / one task | 0 B (uses TCB slot) | 0.4 µs |

Numbers are reproducible — Exercise 2 has you measure four of them on your bench. The kernel side of these latencies is dominated by the critical-section enter/exit (`portDISABLE_INTERRUPTS` / `portENABLE_INTERRUPTS`) plus the optional context-switch trampoline. Stream buffers skip the critical section on the SPSC fast path (a memory barrier and atomic head/tail updates suffice), which is why they are ~50 % faster than the queue path for the same payload.

The "memory per object" column is the kernel control block plus the storage for the items. For a stream buffer with a 256-byte ring you would add 256 B to the 64 B control block, giving 320 B total; for a queue of 4 × 12-byte items you would add 48 B to the 80 B control block, giving 128 B total. Comparable in absolute terms; what matters is the *latency* and the *concurrency contract*.

## Stream buffers — byte-oriented, single-producer / single-consumer

A FreeRTOS stream buffer (`xStreamBufferCreate`, declared in `stream_buffer.h`) is a single-producer single-consumer ring of bytes. Producer calls `xStreamBufferSend(handle, data, length, timeout)` which copies `length` bytes into the ring; consumer calls `xStreamBufferReceive(handle, data, max_length, timeout)` which copies up to `max_length` bytes out. There are no item boundaries: if the producer sends 14 bytes and the consumer receives with `max_length = 200`, it gets 14 bytes (or whatever else is in the ring at the time). If the consumer requests `max_length = 5`, it gets 5 bytes and the remaining 9 stay in the ring for the next call.

The SPSC restriction is real. The internal head and tail pointers are updated by a single thread of each role; if two tasks call `xStreamBufferSend` on the same handle, you have a race. The FreeRTOS docs are explicit (reference manual §5.1.2): "stream buffer functions must not be called by more than one task or interrupt service routine". You can have one ISR producer and one task consumer, or one task producer and one ISR consumer, or one task each — but not two of either.

Why the restriction? Because the SPSC ring lets the kernel skip the critical section on the fast path. The producer-side state is `xHead`; the consumer-side state is `xTail`; each side reads its own pointer freely and reads the other side's pointer with a `__DMB` memory barrier. There is no shared mutable state that requires interrupt masking, and `xStreamBufferSendFromISR` can run while a task is in `xStreamBufferReceive` without locking. Multi-producer would require locking, which would defeat the latency advantage.

In practice, this is exactly what you want for an ISR-to-task path. Your ISR is the sole producer (the hardware fires one interrupt, one ISR runs, it writes to the ring). Your worker task is the sole consumer (one task drains the ring, reads the bytes, processes them). The SPSC restriction maps onto your architecture for free.

### The trigger level

A stream buffer has a *trigger level* — the threshold of bytes-in-ring at which a blocked consumer becomes ready. Default trigger level is 1 (wake on every send). For the MPU-6050 case, every interrupt delivers 14 bytes (1 status byte + 6 accel + 1 temp + 6 gyro). If the trigger level is 1, the consumer wakes 14 times to receive the 14 bytes — 14 context switches, ~84 cycles each, ≈ 9 µs of pure switch overhead per sample. If the trigger level is 14, the consumer wakes once, drains 14 bytes in one `xStreamBufferReceive`, ≈ 0.6 µs of switch overhead.

Tune by:

```c
xStreamBufferSetTriggerLevel(g_mpu_stream, 14);
```

(Or pass the trigger level as the third argument to `xStreamBufferCreate`.) The trigger level can be changed at runtime — useful for adapting to a varying frame size in protocols that have a length-prefix.

A subtlety: the trigger level is a *minimum*. If the ring has 14 bytes when the consumer arrives, it wakes immediately. If it has 56 bytes (4 frames buffered), it wakes immediately and `xStreamBufferReceive` returns up to its `max_length` argument. The trigger level does not cap the returned length; it only gates the wake.

### When to use a stream buffer vs a queue

| Want                                              | Use                                                |
|---------------------------------------------------|----------------------------------------------------|
| Fixed-size structs, multiple producers            | Queue                                              |
| Variable-length byte stream, one producer         | Stream buffer                                      |
| Whole-message semantics on a variable-length stream | Message buffer                                    |
| Bulk-byte transfer (UART RX, SPI RX, ADC stream)  | Stream buffer                                      |
| ISR signalling with no payload                    | Direct-to-task notification, or binary semaphore   |
| Event counting (events may pile up)               | Counting semaphore or notification in counting mode |

For the MPU-6050 EXTI path the stream buffer is the right answer: variable-length data (in case we add INT_STATUS-driven dispatch where some interrupts deliver 6 bytes and some 14), one ISR producer, one task consumer, latency-sensitive.

## Message buffers — stream buffers with a length prefix

A message buffer (`xMessageBufferCreate`) is implemented on top of a stream buffer. The send call prepends a 4-byte length to the user data; the receive call reads the length, returns that many bytes as a single message, and refuses to start delivering the next message until the current one is consumed in full. This gives you whole-message semantics on a byte ring.

The overhead is 4 bytes per message — modest for messages over ~16 bytes, prohibitive for 1- or 2-byte messages. For the MPU-6050 case (14-byte frames), the overhead is 4/14 = 28 %, which is acceptable. For a UART RX path where each interrupt delivers 1 byte, the overhead is 400 %, which is not.

Use a message buffer when your producer has natural message boundaries (each interrupt delivers a complete record) and your consumer wants to process exactly one message per `xMessageBufferReceive` call. Use a stream buffer when the consumer can adaptively process whatever is available.

The mini-project uses a stream buffer with trigger level 14. The stretch goal asks you to switch to a message buffer and compare the context-switch counts.

## Direct-to-task notifications — the wire

Every FreeRTOS task has a *notification state* in its TCB: a 32-bit value (`ulNotifiedValue`) and a 1-bit state (`eNotifyState`, one of `eNotWaitingNotification`, `eWaitingNotification`, `eNotified`). The TCB carries this whether you use notifications or not — it costs you 5 bytes per task regardless. Using notifications, therefore, costs you zero additional memory per signal.

The API (declared in `task.h`):

```c
BaseType_t xTaskNotifyGive(TaskHandle_t xTaskToNotify);
BaseType_t xTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify,
                                  BaseType_t *pxHigherPriorityTaskWoken);

uint32_t  ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait);

BaseType_t xTaskNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue,
                       eNotifyAction eAction);
BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry,
                           uint32_t ulBitsToClearOnExit,
                           uint32_t *pulNotificationValue,
                           TickType_t xTicksToWait);
```

The `eAction` enum has five values:

- `eNoAction` — wake the task but do not change `ulNotifiedValue`. Like a binary semaphore.
- `eSetBits` — OR the given value into `ulNotifiedValue`. Like an event group with 32 bits.
- `eIncrement` — increment `ulNotifiedValue` by 1. Like a counting semaphore.
- `eSetValueWithOverwrite` — set `ulNotifiedValue` to the given value unconditionally. Like a length-1 queue with `xQueueOverwrite`.
- `eSetValueWithoutOverwrite` — set `ulNotifiedValue` to the given value, but only if the previous notification was consumed; otherwise leave it alone and return `pdFALSE`. Like a length-1 queue with `xQueueSend(0 timeout)`.

The notification is delivered to the specific task identified by `xTaskToNotify`. There is no fan-out (use an event group or a semaphore if you need that), there is no broadcast, and the signaller must know which task to wake. For an ISR-to-task pair this is fine — the ISR knows; it was wired to that task at boot.

### Why notifications are faster than semaphores

A binary semaphore is implemented in `queue.c` as a length-1 queue with a 0-byte item size. Giving the semaphore acquires the queue's spinlock-equivalent (a `portENTER_CRITICAL` on M0+), updates the queue's item count and head/tail indices, walks the queue's list of waiting tasks, picks the highest-priority one, removes it from the queue's wait list, adds it to the scheduler's ready list, releases the critical section. Approximately 50 cycles of useful work bracketed by ~30 cycles of critical-section overhead.

A `xTaskNotifyGiveFromISR` skips the queue layer entirely. It enters a (smaller) critical section, finds the task by handle (no list walk — the handle *is* the TCB pointer), updates `eNotifyState` and `ulNotifiedValue`, sets the higher-priority-task-woken flag, exits the critical section. Approximately 25 cycles of useful work bracketed by the same ~30 cycles of critical section. On the M0+ that is ~0.4 µs vs ~0.8 µs — a 2× speedup on a small absolute time, which adds up when you signal 200 times per second.

### When to use notifications instead of semaphores

- Always when there is a single ISR and a single task and you do not need an item payload. The notification is strictly lighter.
- When you want to multiplex events onto one task without using an event group. Use `eSetBits` mode — the ISR sets bits, the task `xTaskNotifyWait`s on any of them, processes whichever ones are set, and clears them on exit.
- When you want to count events without allocating a counting semaphore. Use `eIncrement` mode.

Do *not* use notifications when:

- More than one ISR or task needs to signal the same target task at the same time. The notification slot is single-bit / single-value; the second signaller will collide with the first (depending on the `eAction`, the second may overwrite the first or fail outright).
- You need to fan out a single event to multiple tasks. Use an event group.
- You need to hand off variable-length data. Use a stream or message buffer.

The mini-project's stretch goal is to switch the binary-semaphore signal from the MPU-6050 ISR (in Exercise 2) to a `xTaskNotifyGiveFromISR`, and measure the latency reduction. Expected drop: ~0.4 µs out of a 4 µs end-to-end budget.

## A chooser table you will tape to the wall

This is the most useful single artifact in Week 7. Tape it to your monitor for the rest of C7.

```text
WHAT DO YOU WANT TO HAND OFF?
|
|-- Nothing (just a wake-up signal) ---------------+
|                                                  |
|   How many signallers and how many waiters?      |
|     1 -> 1 ---------------------- xTaskNotifyGive  (fastest)
|     N -> 1 ---------------------- xSemaphoreGive (binary)
|     N -> N (broadcast) ---------- event group
|     N -> 1 with event count ----- xSemaphoreGive (counting)
|
|-- A small fixed-size payload (e.g. 4-byte enum)
|     and 1 ISR -> 1 task ---------- xTaskNotify (eSetValueWithOverwrite)
|     N -> 1 or 1 -> N ------------- xQueueSend (length >= 1)
|
|-- A variable-length byte stream
|     1 ISR -> 1 task -------------- xStreamBufferSend
|     with whole-message semantics - xMessageBufferSend
|
|-- A large struct copied by value
|     -------------------------------- xQueueSend (length 1 + xQueueOverwrite if mailbox-style)
|
|-- A pointer to a buffer the receiver will free
|     -------------------------------- xQueueSend (item type = pointer)
|     plus ownership rule documented as a comment
```

The rule "one signaller and one waiter, no payload → notification" eliminates 80 % of the binary-semaphore usage in a typical Cortex-M0+ firmware. Last week's exercise-02 (button → LED) is one of these cases; the stretch goal rewriting that exercise to use a notification is a good first-day-of-Week-7 warmup.

## A subtle pitfall: the notification clear-on-exit

`ulTaskNotifyTake(xClearCountOnExit, xTicksToWait)` has two modes selected by `xClearCountOnExit`:

- `pdFALSE` — decrements `ulNotifiedValue` by 1 on exit. Behaves like a counting semaphore.
- `pdTRUE` — sets `ulNotifiedValue` to 0 on exit. Behaves like a binary semaphore.

If your producer is firing faster than the consumer can drain *and* you used `pdTRUE`, you lose count: 3 fires between consumes report as one event, not three. If you need to know how many events happened, use `pdFALSE` and decrement once per call.

For the MPU-6050 EXTI case at 200 Hz with a consumer task that drains every 5 ms, you should not be missing events; if you are, the consumer is too slow and there is a deeper bug. But the choice of `xClearCountOnExit` is one of the three or four configuration parameters that, when wrong, make the bug subtle and not obvious. Choose it consciously.

## A second pitfall: `xTaskNotifyWait` with bit-mask actions

If you use `eSetBits` mode, the consumer must use `xTaskNotifyWait` (not `ulTaskNotifyTake`) to read the bits. `ulTaskNotifyTake` decrements / clears the *count*, not the bits. The two API entries are not interchangeable; the kernel does not enforce which one you use, and the bug surfaces as "the task wakes but no bits are visible because I cleared them with the wrong function".

The kernel does enforce that `xTaskNotifyGive` and `ulTaskNotifyTake` are a pair (both touch the count), and `xTaskNotify(eSetBits)` and `xTaskNotifyWait` are a pair (both touch the bits). Mixing across pairs is a configuration bug.

## What this lecture's exercise will show you

Exercise 2 builds the MPU-6050 EXTI ingest: the GP22 ISR posts the 14 sensor bytes (after a blocking I²C read — yes, blocking, in an ISR; we will fix that in Week 8 with DMA) to a stream buffer at trigger level 14, and a consumer task drains the stream buffer and prints the latest reading at 1 Hz under a UART mutex. You will compare three configurations on the same hardware:

1. Stream buffer with trigger level 1 (the default). Consumer wakes 14 times per sample. Context-switch overhead at 200 Hz: ~28 000 switches/sec.
2. Stream buffer with trigger level 14 (the tuned version). Consumer wakes once per sample. Context-switch overhead: ~200 switches/sec.
3. Queue with length 4 × `MPUFrame_t` (the alternative we are *not* using). Context-switch overhead: ~200 switches/sec, but each send is ~1.4 µs instead of ~0.6 µs.

The takeaway is on the SOLUTIONS.md: the trigger-level tune matters more than the primitive choice. A stream buffer at trigger level 1 is *worse* than a queue at length 4; a stream buffer at trigger level 14 is better than either. The kernel cannot guess your trigger level for you.

Next lecture: priority inheritance, the formal bound on the worst-case blocking time, and the deadlock-by-mutex-order pathology that we fix with a global acquisition order. We close the loop on Week 6's mutex-vs-semaphore work.
