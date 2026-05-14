# Lecture 3 — Priority Inheritance, the Blocking Bound, and Deadlock by Mutex Order

> *Last week we replaced a binary semaphore with a mutex and watched the priority-inversion pathology disappear from the Saleae trace. We did not say what the new bound was. This week we say it. Sha, Rajkumar, and Lehoczky proved the bound is the longest critical section any lower-priority task might hold on a mutex you might want. You can compute that number on paper before you write a line of code, and you can verify it on the bench. If your measured worst-case blocking exceeds your computed bound, you have a bug in the analysis, not the kernel.*

## Recap: what priority inversion is and what priority inheritance does

Three tasks H (priority 3), M (priority 2), L (priority 1) and a binary semaphore S guarding a shared resource. L takes S and runs in a critical section. H wakes and tries to take S; blocks. M wakes and runs CPU-bound work; M does not need S; M preempts L. Now L cannot run until M finishes; H cannot run until L finishes; and so H is starved through the whole M run. H's worst-case latency is bounded by M's run time, not by L's critical section. If you swap M out for an arbitrary number of medium-priority tasks, the bound grows without limit. The system becomes *unanalyzable*: you cannot compute the worst-case latency of H without enumerating every possible execution of every other task at every priority strictly below H.

Replace S with a FreeRTOS mutex (`xSemaphoreCreateMutex`). The kernel implements *priority inheritance*: when H blocks on a mutex held by L, the kernel temporarily promotes L to H's priority (3) for as long as L holds the mutex. M can no longer preempt L (M is still at priority 2; L is now at priority 3). L runs its critical section to completion, gives the mutex, drops back to priority 1. H takes the mutex and runs. M runs whenever no higher-priority task is ready.

The worst-case latency of H is now bounded by *L's critical section*, which is a known quantity that you can measure or compute from the source. The system is analyzable again.

## The Sha-Rajkumar-Lehoczky bound

Sha, Rajkumar, and Lehoczky proved (IEEE Transactions on Computers, Vol. 39, No. 9, September 1990, pp. 1175–1185), in their Theorem 3, the following bound under the basic priority-inheritance protocol:

> The worst-case blocking time for a task T of priority p is bounded by the duration of one critical section per mutex, where each critical section is the longest one held by any task of priority strictly less than p on a mutex that any task of priority less than p might hold and that T might also hold.

Symbolically: let `cs(j, k)` be the duration of the critical section that lower-priority task `j` holds on mutex `Mk`. Let `M(T) = { Mk : T might attempt to take Mk }`. Let `L(T) = { j : prio(j) < prio(T) }`. Then:

```
B(T) <= sum over Mk in M(T) of max{ cs(j, k) : j in L(T), j might also take Mk }
```

That is: for each mutex T might want, find the longest critical section any lower-priority task holds on that mutex; sum those longest critical sections; that is the worst-case blocking on T.

A few notes:

- The bound is one critical section *per mutex*, not one per lower-priority task. If three lower-priority tasks each hold mutex M1 in turn, T waits for at most one of them (the longest), not all three. The kernel's priority-inheritance promotes whichever one is currently holding it, and the others cannot preempt the promoted holder.
- The bound assumes the *basic* priority-inheritance protocol, which is what FreeRTOS implements. There is a more sophisticated *priority-ceiling protocol* (also Sha-Rajkumar-Lehoczky 1990) that bounds blocking to the longest *single* critical section across all mutexes T might want — a tighter bound at the cost of more bookkeeping. FreeRTOS does not implement it. The cost of the basic protocol's looser bound is rarely material.
- The bound applies only to mutexes T might *want*. If T never takes M2, then T does not block on M2, and M2 does not contribute to B(T). This is why minimizing the set M(T) — keeping critical sections short and the per-task mutex set small — is good design.

## Worked example

The Week 6 mini-project had three tasks (sensor 3, transform 2, printer 1) and one mutex (uart_mutex). The transform and the printer both took the mutex. The sensor did not.

For the printer (priority 1), the only lower-priority task is the idle task; the idle task does not take the mutex; therefore L(T) is empty for the printer and B(printer) = 0.

For the transform (priority 2), L(T) = { printer }. The printer takes uart_mutex. The longest critical section the printer holds is the `printf` of one formatted line — let's call it 80 µs. So B(transform) ≤ 80 µs.

The sensor (priority 3) does not take the mutex. M(sensor) is empty. B(sensor) = 0.

So the worst-case blocking time of the sensor is 0 (no mutex contention), of the transform is 80 µs (one mutex, one lower-priority taker), and of the printer is 0 (no lower-priority tasks of concern). The transform's deadline of 100 ms (period at 10 Hz) easily absorbs 80 µs.

## The Week 7 mini-project bound

The Week 7 mini-project has four tasks:

- vIngest at priority 4 — drains the stream buffer from the MPU-6050 ISR.
- vEstimator at priority 3 — consumes the stream buffer, emits a quaternion.
- vDownsample at priority 2 — averages the quaternion at 10 Hz.
- vGatekeeper at priority 1 — owns the UART.

And two mutexes:

- m_i2c — protects the I²C bus (used by the ingest task; also used by a hypothetical config-reload task that we sketch in Lecture 3 §"The fourth mutex you forgot")
- m_quaternion — protects the latest-quaternion buffer (used by vEstimator to write, vDownsample to read)

Per-task blocking budget:

| Task        | Mutexes it takes         | L(T)                           | B(T) bound                  |
|-------------|--------------------------|--------------------------------|-----------------------------|
| vIngest     | m_i2c                    | { vEstimator, vDownsample, vGatekeeper }    | cs of slowest other taker of m_i2c |
| vEstimator  | m_quaternion             | { vDownsample, vGatekeeper }   | cs of slowest other taker of m_quaternion |
| vDownsample | m_quaternion             | { vGatekeeper }                | 0 (vGatekeeper does not take m_quaternion) |
| vGatekeeper | (none)                   | (no lower priority)            | 0                            |

In the canonical design, vEstimator and vDownsample are the only takers of m_quaternion. The estimator's critical section is one struct copy, ~10 µs; the downsample's critical section is the same, ~10 µs. So B(vEstimator) ≤ 10 µs.

If we add a config-reload task at priority 1 that takes m_i2c to write configuration to the MPU, B(vIngest) ≤ (the longest config write), which might be 200 µs (multiple register writes). That is the largest single number in the budget; reduce it by either making the config writes shorter or by making the config-reload task not exist (do all config writes at boot, before the scheduler starts).

The mini-project's `BLOCKING-BUDGET.md` deliverable is this analysis, derived statically from the code, plus the measured peak blocking from a bench run. The two numbers should agree to within ~10 %; if they do not, *one of them is wrong* and you have to figure out which.

## How priority inheritance interacts with multiple mutexes

A nuance: priority inheritance is *transitive* in FreeRTOS. If task L holds m1 and is waiting on m2 (held by task L2 at priority < L), and task H tries to take m1, then L is promoted to H's priority — but does that promotion propagate to L2?

The FreeRTOS implementation: yes, kind of. The promotion of L bumps its scheduling priority, which lets it run (preempting any M between it and the CPU). But L is *waiting on m2*, so it cannot run. The kernel does not re-promote L2 automatically. This means transitive blocking through mutex chains is *not* fully bounded by priority inheritance under FreeRTOS — there is a longer worst-case path through m2.

The practical advice: avoid nested mutex takes. If you must nest, take them in a globally-fixed order (see next section). Better: do not let one task hold two mutexes at once; if you need to compose two protected resources, copy out from one, release the first mutex, take the second, copy out, release. Now no task ever holds more than one mutex at a time and you cannot deadlock or chain-inherit.

## Deadlock by mutex order

The classical pathology:

```c
/* Task A: */
xSemaphoreTake(m_alpha, portMAX_DELAY);
xSemaphoreTake(m_beta,  portMAX_DELAY);
/* ... critical section ... */
xSemaphoreGive(m_beta);
xSemaphoreGive(m_alpha);

/* Task B: */
xSemaphoreTake(m_beta,  portMAX_DELAY);    /* opposite order */
xSemaphoreTake(m_alpha, portMAX_DELAY);
/* ... critical section ... */
xSemaphoreGive(m_alpha);
xSemaphoreGive(m_beta);
```

If A takes m_alpha and is preempted by B, and B takes m_beta and then tries to take m_alpha, B blocks (A has it). Now A unblocks, tries to take m_beta, blocks (B has it). Both wait forever. The kernel does not detect this. The system appears alive (the scheduler is running, the idle task is happily executing) but A and B are gone.

The fix is a *global acquisition order*. Pick any total order on mutexes (the simplest is by address: `m_alpha` is at address 0x20002000 and `m_beta` is at address 0x20002040, so we always acquire the lower address first). Enforce it everywhere by review:

```c
/* Task A: */
xSemaphoreTake(m_alpha, portMAX_DELAY);   /* lower address first */
xSemaphoreTake(m_beta,  portMAX_DELAY);

/* Task B: */
xSemaphoreTake(m_alpha, portMAX_DELAY);   /* lower address first */
xSemaphoreTake(m_beta,  portMAX_DELAY);
```

Now A and B both try m_alpha first. Whichever gets it goes first; the other blocks on m_alpha. Once the first releases both, the second wakes, takes m_alpha, then m_beta. Deadlock is impossible.

This works because deadlock requires a cycle in the wait-for graph; a total order on mutex acquisition makes cycles impossible (you cannot wait for an earlier-in-order mutex while holding a later-in-order one).

Challenge 1 has you reproduce the deadlock with `printf` traces, then fix it with the address-order rule.

### Static analysis for the deadlock check

The fix above relies on code review. A more robust approach: use a static analyzer that warns when mutexes are acquired in different orders across functions. `cppcheck` does not catch this; `clang-analyzer` with `alpha.unix.Mutex` will. Coverity catches it. For a class project, code review is the realistic answer.

## The gatekeeper task — eliminating the mutex entirely

The other strategy for resource protection is to not have a mutex at all. Pick one task to own the resource; route every other task's access through that task's queue.

In schematic form:

```
   vEstimator -->\
                  +--> [uart_queue] --> [vGatekeeper] --> UART hardware
   vDownsample ->/
   vIngest     ->/
```

Every task that wants to print formats its line into a buffer, calls `xQueueSend(uart_queue, &buffer, 0)`, returns. The gatekeeper task blocks on `xQueueReceive(uart_queue, &buffer, portMAX_DELAY)`, dequeues, writes to the UART, repeats.

Advantages over the mutex pattern:

1. **No priority inheritance complexity.** The gatekeeper runs at one fixed priority. There is no temporary promotion. The blocking bound on any task that posts to the gatekeeper is `cs(longest single dequeue + UART write) + (queue depth) * (same)`, which is straightforward.
2. **No mutex-order deadlock.** There are no nested mutex takes because there are no mutex takes at all.
3. **No forgot-to-give-back-the-mutex bug.** The mutex pattern requires every taker to give the mutex back on every code path, including error paths. The gatekeeper does the give-back implicitly (it is the only one with the resource).
4. **Natural backpressure.** If the gatekeeper is slow, the queue fills, producers see `xQueueSend` time out, they have a choice about how to handle the backpressure (drop the message, block the producer task, summarize multiple messages into one). With a mutex you have only "block on the take" as the response.

Disadvantages:

1. **One extra context switch per access.** The producer enqueues (one context switch on the way back to its work), the gatekeeper dequeues and writes (one context switch when it next runs). On a busy system the gatekeeper is already runnable and the switch is essentially free; on a quiet system it adds ~1 µs per print.
2. **Memory cost of the queue.** A queue of length 8 with 80-byte string items is 640 bytes. The mutex is 80 bytes. The 560-byte difference matters on tiny parts; on the Pico's 264 KB of RAM it is invisible.

For the Week 7 mini-project we use the gatekeeper. The Week 6 mini-project used a mutex; the side-by-side comparison is in the `GATEKEEPER-NOTE.md` you write as part of the deliverable.

## When *not* to use a gatekeeper

The gatekeeper pattern serializes access at the queue level. If you have a resource where two *concurrent* writers are fine (e.g. an SPI bus where each transfer is independent and the device knows whose data is whose by a chip-select), the gatekeeper's serialization is a needless bottleneck. In that case the mutex is the right pattern.

For the UART case the serialization is the *point*: two producers' bytes interleaved on the wire are corrupt. The gatekeeper's serialization is exactly the property we want.

## The `INHERIT_PRIORITY` configuration option that does not exist

FreeRTOS does not have a configuration option to disable priority inheritance on mutexes. It is always on. If you want a mutex-like API without priority inheritance, you use `xSemaphoreCreateBinary` and live with the unbounded-inversion possibility. (Don't.)

This is intentional: the kernel authors take the position that a mutex *means* "I am protecting a resource and I want priority inheritance"; if you want a signal, use a binary semaphore; if you want a resource lock, use a mutex; do not blur the line. We agree.

## What this lecture's exercise will show you

Exercise 3 has you construct a worst-case blocking scenario with three tasks (high, medium, low priority) and one mutex, derive the bound on paper, run the firmware, measure the actual peak blocking with GPIO toggles, and verify the two numbers agree.

Challenge 1 has you build the two-mutex deadlock and capture it on a logic analyzer (the GPIO pattern stops moving), then fix it with the address-order rule and re-capture.

The mini-project's `BLOCKING-BUDGET.md` is your written analysis of the four-task, two-mutex pipeline. The format is:

```text
For each task T at priority p:
  M(T) = { mutexes T might take }
  L(T) = { lower-priority tasks }
  For each mutex Mk in M(T):
    longest_cs(Mk) = max over j in L(T) of cs(j, k)
  B(T) = sum of longest_cs(Mk) over Mk in M(T)
```

Plus the measured peak from a one-hour run. The two numbers should agree to within 10 %; if they do not, *find out why*. The most common cause of "measured peak > computed bound" is an undocumented mutex take (someone added a `xSemaphoreTake` in a debug print and forgot it counts toward the bound).

Next: the exercises. Bring up the simplest possible EXTI handoff, wire the MPU-6050, build the worst-case blocking scenario, and capture each step on the Saleae.
