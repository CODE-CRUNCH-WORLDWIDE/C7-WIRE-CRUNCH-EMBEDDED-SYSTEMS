# Week 7 — Homework

Six practice problems. Each should take 45–90 minutes. The first three are conceptual / written; the last three are bench tasks that build on the exercises and the mini-project. Commit your written answers and bench artefacts in a `homework/` subdirectory of your week-07 repo.

---

## Problem 1 — Primitive chooser

You are designing the ISR-to-task path for each of the following five sensors on the same Pi Pico W. Pick the FreeRTOS primitive that minimizes context-switch overhead and CPU cost while preserving correctness. Justify in one sentence per choice.

| Sensor                                  | Rate       | Per-event payload     | One ISR / multiple? | Recommended primitive |
|-----------------------------------------|------------|-----------------------|---------------------|-----------------------|
| MPU-6050 IMU data-ready                 | 200 Hz     | 14 bytes              | one ISR             | (you fill in)         |
| UART RX FIFO half-full                  | 92 KB/s    | 16 bytes / event      | one ISR             |                       |
| Rotary-encoder edge                     | up to 10 kHz | 1 bit direction      | one ISR             |                       |
| External RTC alarm                      | 1 Hz       | none                  | one ISR             |                       |
| Multiple buttons sharing one GPIO ISR   | event      | enum of which button  | one ISR (dispatches)|                       |

Deliverable: `homework/p1-chooser.md` with the filled table and one-sentence justifications. The primitive set is { stream buffer, message buffer, queue, binary semaphore, counting semaphore, direct-to-task notification (with action chosen) }.

Rubric:
- 5 sensors × 4 points each = 20 points.
- −2 if a sentence does not explain *why* the chosen primitive is preferred over the next-closest alternative.

---

## Problem 2 — Static blocking-budget derivation

Take the Week 6 mini-project's source (your three-task sensor pipeline) and compute the priority-inversion bound `B(T)` for each task by static inspection. Use the format from Lecture 3.

```text
For each task T at priority p:
  M(T) = { mutexes T might take }                       # by reading the source
  L(T) = { tasks at strictly lower priority }
  longest_cs(Mk) = max over L(T) of cs(j, k)
  B(T) = sum of longest_cs(Mk) over Mk in M(T)
```

For each `cs(j, k)`, estimate the duration in microseconds using a 1 statement = 10 cycles rule of thumb plus actual API times for any FreeRTOS or pico-sdk call in the critical section (look these up; cite the FreeRTOS reference manual section).

Deliverable: `homework/p2-week6-blocking-budget.md` with one section per task. End with a "Total worst-case latency" table:

| Task | Period | Own work | B(T) | Total | Within budget? |

Rubric:
- 3 tasks × 5 points = 15 points. Full points for a correct derivation with cited times.
- −3 if any `cs(j, k)` is given without a citation for the API time.
- −5 if the derivation conflates priority-inversion blocking with simple non-mutex preemption.

---

## Problem 3 — Power-budget tradeoff

You are deciding between three idle-hook strategies for a battery-powered Pico W product:

- A: empty idle hook (the kernel spins).
- B: `__wfi();` in the idle hook.
- C: `sleep_us(100);` in the idle hook (yields 100 µs of low-power sleep per idle pass).

For each strategy, give:

- Average current draw on 3V3 (use measurement from the lecture or the Memfault Interrupt blog as a baseline).
- Worst-case added ISR-latency jitter (cite from the RP2040 datasheet or measurement).
- One scenario where the strategy is the right pick.

Deliverable: `homework/p3-power-tradeoff.md`, 1 page, with a small table and three paragraphs.

Rubric: 15 points (5 per strategy). Full marks for a citable number for current draw and latency.

---

## Problem 4 — Bench: priority-inheritance bound

Run Exercise 3 (`exercise-03-priority-inheritance-bound.c`) twice — once with `USE_MUTEX = 1` and once with `USE_MUTEX = 0`. Capture 60 seconds of `GP10, GP11, GP12, GP13` on the Saleae for each run. Add a measurement view that reports the time from rising `GP13` (H blocked) to falling `GP13` (H acquired).

Deliverable:
- `homework/p4-priority-bound-mutex.sal`
- `homework/p4-priority-bound-semaphore.sal`
- `homework/p4-analysis.md` containing:
  - Derived bound `B(H)` (paper).
  - Measured median, p99, max for the mutex case.
  - Measured median, p99, max for the semaphore case.
  - One-paragraph explanation of why the two distributions look the way they look.

Rubric:
- 20 points total. 5 for each capture, 5 for the derived bound, 5 for the explanation.
- −5 if the measured peak exceeds the derived bound and the analysis does not explain why.

---

## Problem 5 — Bench: gatekeeper vs mutex

Take the Week 6 mini-project (UART mutex) and the Week 7 mini-project starter (UART gatekeeper). Run each for 30 seconds with the same three "client" tasks posting 100 lines/sec each. Capture the UART line on the Saleae and decode as 8N1 at 115200 baud.

For each capture, compute:

- Number of lines transmitted in 30 s.
- Standard deviation of inter-line gap (the time from end-of-line to start-of-next-line).
- p99 of the time from "task buffer-formatted" to "first byte of that line on the wire".

Deliverable: `homework/p5-gatekeeper-vs-mutex/` directory with:

- `mutex.sal`
- `gatekeeper.sal`
- `comparison.md` with the table above and a one-paragraph verdict.

Rubric:
- 20 points. 5 per metric measured, 5 for the verdict.
- The verdict should call out the higher per-access latency of the gatekeeper and the lower analytical complexity. A verdict that gets the latency direction wrong (claiming the gatekeeper is lower-latency per access) loses 10 points.

---

## Problem 6 — Stretch: implement a priority-ceiling-protocol mutex

The FreeRTOS mutex implements the basic priority-inheritance protocol. The priority-ceiling protocol (Sha-Rajkumar-Lehoczky 1990, the other major contribution of that paper) bounds blocking more tightly: a task that wants to take a mutex is blocked *before* the take if its priority is not strictly greater than the ceiling of every currently-held mutex. The bound is the longest single critical section across all mutexes, not the sum.

Implement a thin wrapper around `xSemaphoreCreateMutex` that enforces the ceiling-protocol pre-check at every take. The API:

```c
PcpMutex_t pcp_create(UBaseType_t ceiling);
BaseType_t pcp_take(PcpMutex_t m, TickType_t timeout);
BaseType_t pcp_give(PcpMutex_t m);
```

`ceiling` is the highest priority of any task that might take the mutex; statically known. `pcp_take` checks the current task's priority against a system-wide "current ceiling" (the max ceiling of all currently-held mutexes) and blocks if not strictly greater.

Deliverable: `homework/p6-pcp/` directory with `pcp.c`, `pcp.h`, a test program demonstrating that:

- Tasks at priority > current_ceiling can take the mutex.
- Tasks at priority ≤ current_ceiling are blocked at the wrapper layer (not inside the underlying mutex).
- The Sha-Rajkumar-Lehoczky bound (one critical section, not a sum) is honored.

Rubric:
- 30 points. 10 for a working implementation, 10 for the test, 10 for a README that explains why the basic priority-inheritance bound is looser than the priority-ceiling bound and cites the paper.

Stretch: this problem is worth 30 points but is harder than the other five. Skip it if you are under time pressure; the core Week 7 deliverable does not require it.

---

## Submission

Tag your week-07 GitHub release as `v0.7-homework` and push. Open a PR against the `course-feedback` branch in the cohort review repo with a link to your tag. Include the rubric table at the top of your PR description so the reviewer can mark per-problem.

Total possible: 120 points across 6 problems. 90+ is "ready for Week 8". 80–89 is "review the lecture you scored worst on, then proceed". Below 80 is "redo before Week 8".
