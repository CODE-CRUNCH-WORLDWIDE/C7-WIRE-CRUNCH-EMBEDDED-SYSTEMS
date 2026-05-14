# Lecture 1 — What an RTOS is, and what it isn't

> *An RTOS is a scheduler plus a handful of synchronization primitives, packaged with a contract: every API call has a worst-case execution time you can write down. That is the whole product.*

## The shortest possible definition

An *operating system* manages resources — CPU, memory, peripherals, files, network — on behalf of programs that do not want to manage them directly. A *real-time operating system* is an operating system that additionally guarantees the *time bound* on every scheduling decision and every kernel call. The first half of the name (Operating System) describes what it does. The second half (Real-Time) describes the contract it makes about *when*.

The contract is the load-bearing part. A general-purpose OS — Linux, macOS, Windows — schedules for *average* throughput; it will happily delay a 1 ms deadline by 200 ms if doing so improves the system's average performance over the next minute. An RTOS schedules for *worst-case* deadline; it will happily reduce average throughput by 30 % if doing so guarantees the 1 ms deadline never misses. Same hardware, opposite optimization target.

FreeRTOS in particular is a real-time *kernel* — the scheduling-and-synchronization core of an RTOS, without the filesystem, network stack, shell, or driver framework that a full RTOS would also include. About 9 000 lines of MIT-licensed C. The kernel is the whole product. If you want a filesystem, you bring `FATFS` or `LittleFS`. If you want TCP, you bring `lwIP`. The kernel does not assume them.

## The three flavours of real-time

The term *real-time* is overloaded. Three meanings, in order of strictness:

- **Hard real-time.** A missed deadline is a system failure. The system either operated correctly within the deadline or it operated incorrectly, full stop. There is no graceful degradation: a fuel-injection ignition pulse that arrives 100 µs late causes engine knock; a defibrillator pulse that arrives 10 ms late is medically meaningless; a brake-by-wire actuation that arrives 50 ms late is a crash. Hard-real-time systems are typically *safety-critical* and certified to IEC 61508 (industrial), ISO 26262 (automotive), DO-178C (aerospace), or IEC 62304 (medical).
- **Firm real-time.** A missed deadline is non-catastrophic but the result becomes useless. A video decoder that misses a frame's deadline drops the frame; the user sees a brief stutter. A robot arm that misses a position update skips that update; the next one will correct. Firm-real-time systems care about *deadline-miss frequency* — drop one frame per second is fine, drop one per ten is intolerable — but a single miss does not kill the system.
- **Soft real-time.** A missed deadline degrades quality but the result remains useful. A web browser laying out a page in 50 ms instead of 30 ms is slower but still correct. A 1 Hz UART status print arriving 30 ms late is unremarkable. Soft-real-time systems use deadlines as *guidance to the scheduler* rather than *enforcement contracts*.

The same hardware, same kernel, and same firmware can host all three categories of task simultaneously. In this week's mini-project: the BMP280 sensor read at 10 Hz is firm (a missed read is dropped, the next one is fine), the transform-and-queue-post at 10 Hz is firm (it must keep up with the sensor), the UART print at 1 Hz is soft (nobody minds if the timestamp is 20 ms off). None of these tasks is hard-real-time. If your capstone is a brushless-motor commutation loop at 20 kHz, the commutation step *is* hard-real-time and you will treat it differently — likely as a DMA + PIO state-machine arrangement that bypasses the RTOS entirely for the inner loop, with the RTOS handling the housekeeping.

## What the kernel actually contains

A FreeRTOS image on a Cortex-M0+ is approximately:

| Component                          | Source file                     | LOC  | Code size (`-Os`) |
|------------------------------------|---------------------------------|-----:|------------------:|
| Scheduler core                     | `tasks.c`                       | 4500 |             ~5 KB |
| Queues, semaphores, mutexes        | `queue.c`                       | 1800 |             ~2 KB |
| Software timers                    | `timers.c` (optional)           |  700 |           ~600 B  |
| Event groups                       | `event_groups.c` (optional)     |  600 |           ~500 B  |
| List operations                    | `list.c`                        |  200 |           ~200 B  |
| Cortex-M0+ port                    | `portable/GCC/ARM_CM0/port.c`   |  400 |           ~400 B  |
| Heap (one of `heap_1`..`heap_5`)   | `portable/MemMang/heap_*.c`     |  ~300 |          ~400 B  |
| **Total (typical)**                |                                 | ~8000 |          **~8 KB** |

That is the whole kernel. Read every line of `tasks.c` once during your career; it is the most important 4 500 lines of embedded C you will encounter. The scheduler is not magic.

What the kernel *does not* contain:

- A filesystem. Pico has 2 MB of QSPI flash but no filesystem unless you link one (`LittleFS` is the common choice for raw flash, `FATFS` for SD cards). FreeRTOS treats flash as opaque bytes.
- A network stack. `lwIP` integrates cleanly with FreeRTOS (`sys_arch.c` for FreeRTOS is part of the lwIP `contrib/` tree) but is a separate ~30 KB code blob.
- A USB stack. The Pico SDK ships TinyUSB, which works alongside FreeRTOS but is again a separate ~20 KB blob.
- Driver frameworks. There is no Linux-style "driver model" with hot-plug and probe; you write each driver as a task or a callback and the kernel does not know it exists.
- Memory protection. Cortex-M0+ has no MPU. The kernel does not enforce inter-task memory isolation; any task can wander into any other task's stack with a stray pointer. (The Cortex-M3/M4/M7 cores have an MPU and FreeRTOS has a `mpu_wrappers.c` for them. Out of scope for Week 6.)
- Symmetric multiprocessing. The RP2040 has two M0+ cores; the standard FreeRTOS port pins the kernel to core 0. The SMP variant (`configNUMBER_OF_CORES > 1`, added 2023) does support both, but with caveats. Out of scope for Week 6.

The set of features the kernel *does* provide is small and finite:

- Tasks. Create, delete, suspend, resume, change priority, set a delay until a tick count, set a periodic delay relative to the previous wake.
- Queues. Create, send (block / non-block / FromISR), receive (block / non-block / FromISR), peek, overwrite-on-full, query length.
- Semaphores. Binary, counting; give, take, FromISR variants.
- Mutexes. Like binary semaphores but with priority inheritance and ownership.
- Software timers. One-shot and periodic; run in a timer-service task.
- Event groups. 24-bit bitfields; tasks wait on any-of or all-of patterns.
- Direct-to-task notifications. A lighter-weight signal-and-data path that avoids the overhead of creating a queue or semaphore for one-to-one signalling.
- Hooks. `vApplicationIdleHook`, `vApplicationTickHook`, `vApplicationStackOverflowHook`, `vApplicationMallocFailedHook`.

This week we use tasks, queues, binary semaphores, and mutexes. Software timers, event groups, and notifications are covered in Week 7.

## Preemption: the central design decision

A scheduler picks which of the ready tasks runs next. Two regimes:

**Cooperative scheduling.** A running task continues to run until it voluntarily yields — by calling `taskYIELD()`, by blocking on a queue or semaphore, or by sleeping with `vTaskDelay`. The kernel never interrupts a task that is not asking to be interrupted. This makes the scheduler simple and predictable; it also means a task with a bug (an infinite loop, a long computation, a busy-wait) hangs the entire system until the next reset. Cooperative scheduling is selected by `configUSE_PREEMPTION = 0` in `FreeRTOSConfig.h`. It is almost always the wrong choice.

**Preemptive scheduling.** The kernel can interrupt a running task at any tick boundary — or at any interrupt that wakes a higher-priority task — and switch to a different task. The current task did not consent; the kernel saved its register file on entry to the ISR and now restores a different task's register file before returning. The default on FreeRTOS. The default for nearly every shipping RTOS on the planet. Preemption is what makes the priority assignment *work*: the high-priority sensor task can take the CPU away from the low-priority UART task without the UART task's cooperation.

A subtlety inside preemptive scheduling: what happens when *two* ready tasks have the *same* priority? Two answers, selected by `configUSE_TIME_SLICING`:

- `configUSE_TIME_SLICING = 1` (default). The kernel round-robins through the same-priority tasks, switching on each tick. If three tasks at priority 2 are all ready, the scheduler runs each one for one tick, then moves to the next. This is sometimes called "preemptive priority with round-robin within a priority level".
- `configUSE_TIME_SLICING = 0`. The first same-priority task that becomes ready runs until it blocks or is preempted by a higher-priority task. The other same-priority tasks wait. Used in cases where context-switch overhead matters more than fairness.

For the mini-project you can leave this at the default. Just be aware that if you assign two tasks the same priority and one of them never blocks, the other one starves until the first finally blocks. Don't do that.

## Priorities and the rate-monotonic argument

FreeRTOS supports `configMAX_PRIORITIES` (default 5 on the Pico SDK port; we set it to 8 in this week's project to keep room for growth). Priority 0 is the lowest (the idle task lives there). Higher numbers = higher priority. The scheduler always picks the highest-priority ready task.

The question is how to *assign* priorities to your tasks. The classical answer, valid whenever your tasks are roughly periodic and independent, is **rate-monotonic scheduling**:

> The task with the shortest period gets the highest priority.

Liu and Layland (1973) proved this is optimal among fixed-priority schedulers: if any fixed-priority assignment can meet all deadlines, the rate-monotonic assignment will. They also proved a worst-case utilization bound: for N independent periodic tasks with deadlines equal to periods, the rate-monotonic schedule guarantees feasibility as long as total CPU utilization is below `N · (2^(1/N) - 1)` — about 83 % for N = 2, 78 % for N = 3, 69 % asymptotically. Below the bound: provably feasible. Above the bound: maybe feasible, maybe not — you need a more careful analysis (Liu-Layland's *exact characterization* or a response-time analysis).

The mini-project's three periodic tasks:

| Task                    | Period   | Priority (RM) |
|-------------------------|---------:|--------------:|
| BMP280 sensor read      |  100 ms  |             3 |
| Fixed-point transform   |  100 ms  |             2 |
| UART print              | 1000 ms  |             1 |

The sensor and transform have the same period; rate-monotonic does not strictly distinguish them. We give the sensor the higher priority because it has the tighter deadline — the transform consumes from a queue and is naturally rate-limited by the sensor, so giving the sensor the slight edge ensures the queue never starves.

Total CPU utilization in this project is < 5 %, so we are nowhere near the rate-monotonic bound. The priority assignment matters for *latency* (the time from a sensor's `vTaskDelayUntil` wake to the queue post is bounded by the worst-case priority-1 task hold) more than for *feasibility*.

## The scheduling state machine

Every task in FreeRTOS is in exactly one of five states. The full state diagram:

```
                  vTaskCreate
                       |
                       v
            +---------------------+
   +------> |       READY         | <----------------+
   |        +---------------------+                  |
   |             |             ^                     |
   |   chosen by |             | preempted, or       |
   |   scheduler |             | timeslice expired   |
   |             v             |                     |
   |        +---------------------+                  |
   |        |      RUNNING        |                  |
   |        +---------------------+                  |
   |             |             |                     |
   |   blocked   |             | suspended           |
   |  (queue,    |             | (vTaskSuspend)      |
   |  sem, delay)|             |                     |
   |             v             v                     |
   |        +---------+   +-----------+              |
   |        | BLOCKED |   | SUSPENDED |              |
   |        +---------+   +-----------+              |
   |             |             |                     |
   |   event /   |             | vTaskResume         |
   |   timeout   v             v                     |
   +---<---------+-------------+---------------------+

   (DELETED — terminal — once vTaskDelete is called the TCB is freed
    and the task no longer exists.)
```

The states map onto concrete API calls:

- READY → BLOCKED: `xQueueReceive(q, ..., portMAX_DELAY)`, `xSemaphoreTake(s, 10)`, `vTaskDelay(100)`, `vTaskDelayUntil`.
- BLOCKED → READY: the event the task was waiting for occurs (queue gets an item, semaphore gets a give, delay expires).
- READY → SUSPENDED: another task calls `vTaskSuspend(theHandle)`.
- SUSPENDED → READY: another task calls `vTaskResume(theHandle)`.
- Anything → DELETED: `vTaskDelete(theHandle)`. The idle task eventually reclaims the TCB and stack memory; if you do not run the idle task (e.g. you suspend the scheduler indefinitely), memory leaks.

The RUNNING state is special — exactly one task is RUNNING at any time, and the others that *could* run are in READY. The scheduler's only job is to keep picking the right one.

## What the kernel is *not*

A few common misconceptions worth flagging early:

- **"FreeRTOS is real-time, so it is fast."** It is *predictable*, which is not the same as fast. A context switch on Cortex-M0+ takes ~84 cycles plus interrupt latency — about 700 ns at 125 MHz. That is fast enough to be invisible at the millisecond scale of typical tasks, but the *value* is the predictability: every context switch takes the same number of cycles, regardless of how loaded the system is. A Linux scheduler can be faster on average and much slower in the worst case.
- **"An RTOS replaces my superloop."** Sometimes. If your superloop is genuinely a polling loop with no concurrency requirements — three sensors, one print, all at the same rate — keep the superloop. The RTOS is the right answer when you have *independent* timing requirements (different rates, different priorities, different blocking durations) that the superloop cannot express cleanly. The mini-project is in the second category; the Week 1 blink + UART was in the first.
- **"The RTOS prevents race conditions."** It does not. The RTOS *provides* the primitives (queues, mutexes) that let you write race-free code; it does not enforce their use. A naked `shared_counter++` from two tasks is still a race, and the kernel will not warn you. The mutex in Exercise 3 exists because we put it there.
- **"The RTOS makes my code easier to debug."** It makes the design clearer, often at the cost of debugging-time complexity. A bug in a single-threaded superloop is reproducible by inspection. A bug in a five-task RTOS with three queues and two mutexes may depend on the exact tick at which two events collided, and reproducing it requires tooling (Tracealyzer, `vTaskGetRunTimeStats`, `gdb` with the FreeRTOS thread-aware add-on). Plan for this.
- **"Tasks are like threads."** Closer to "threads" than to "processes" — they share an address space, the kernel does not isolate their memory. Closer to "coroutines" than to "OS threads" — they are scheduled by a cooperative-with-preemption userspace scheduler, not by a kernel. The Cortex-M0+ has no MMU, so there is no process/thread distinction the way Linux has it.

## What you do this week

Lecture 2 walks the task-and-stack model in detail: how `xTaskCreate` builds a TCB, how the stack is initialized so the first context switch restores correctly, how the idle task and the idle hook interact, and how the stack-overflow check actually catches things. Lecture 3 walks IPC — queues, semaphores, mutexes — and the priority-inheritance protocol that makes mutexes safe under priority inversion.

Exercise 1 is the simplest possible RTOS program: two LEDs blinking at different rates, one task each. Exercise 2 introduces ISR-to-task hand-off through a queue. Exercise 3 introduces resource protection with a mutex around the UART. Challenge 1 demonstrates the priority-inversion bug explicitly and fixes it. The mini-project assembles all of the above into a three-task sensor pipeline.

By Sunday you will be able to answer, on a quiz, "what is the difference between a binary semaphore and a mutex, and why does it matter?" in one sentence. The sentence is: *a mutex implements priority inheritance and a binary semaphore does not, so a mutex is safe to use for resource protection and a binary semaphore is not.* Memorize the sentence. The quiz will ask.

## Further reading

- "Mastering the FreeRTOS Real Time Kernel" Chapter 1 (Introduction) and Chapter 2 (Heap memory management) — about 30 pages. Skim Chapter 2 unless you plan to switch allocators.
- FreeRTOS Kernel documentation, "What is an RTOS?" — about 10 minutes:
  <https://www.freertos.org/about-RTOS.html>
- The Mars Pathfinder priority-inversion case study (Mike Jones, 1997). Required reading before Challenge 1:
  <https://www.cs.unc.edu/~anderson/teach/comp790/papers/mars_pathfinder_long_version.html>
- Liu and Layland, "Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment", J. ACM 20(1), Jan 1973. The rate-monotonic paper. Read the abstract, §3, §6.
