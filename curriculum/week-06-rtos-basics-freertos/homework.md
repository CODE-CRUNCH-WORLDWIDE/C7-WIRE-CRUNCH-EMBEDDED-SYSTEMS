# Week 6 — Homework

Six practice problems. Each should take 30–90 minutes. The first four are conceptual / written; the last two are bench tasks that build on the exercises and the mini-project. Commit your written answers and bench artefacts in a `homework/` subdirectory of your week-06 repo.

---

## Problem 1 — Rate-monotonic priority assignment

You are designing a Pi Pico W flight controller with five periodic tasks:

| Task                | Period (ms) | Worst-case run time (ms) |
|---------------------|------------:|-------------------------:|
| IMU read            |           2 |                      0.5 |
| Attitude estimation |          10 |                      1.5 |
| Motor commutation   |           1 |                      0.2 |
| Radio link RX       |          20 |                      2.0 |
| Telemetry TX        |         100 |                      5.0 |

(a) Assign rate-monotonic priorities (highest cadence = highest priority).
(b) Compute the total CPU utilization. Is it below the Liu-Layland bound for N=5 (about 74.3 %)?
(c) If yes, rate-monotonic schedulability is *guaranteed*. State this in one sentence with the precise Liu-Layland citation.
(d) If you were forced to add a 1 Hz battery-monitor task that takes 50 ms (5 % of its period), at what priority would you place it and what would the new utilization be?

Cite: Liu and Layland, "Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment", J. ACM 20(1), Jan 1973.

Deliverable: a 1-page Markdown file `homework/p1-rate-monotonic.md` with the table, the utilization arithmetic, and the two answers.

---

## Problem 2 — Mutex hold-time audit

Take the mini-project's source code (after you have built it) and audit every `xSemaphoreTake` call against the matching `xSemaphoreGive`. For each mutex hold, write down:

- The maximum number of statements executed inside the critical section.
- The maximum estimated time the critical section can take (using a 10 cycles/statement rule of thumb plus actual API call times for any kernel APIs invoked inside).
- The worst-case priority inversion bound for any task that might block on this mutex.

Format the output as a `homework/p2-mutex-audit.md` table:

| Mutex name | File:line of take | File:line of give | Max statements | Estimated max hold (us) | WCI bound (us) |

The mini-project has exactly one mutex (the UART mutex); if your stretch-goal implementation has more, audit them all.

The point of the exercise: priority-inheritance bounds priority inversion to *the length of the longest critical section*. You must be able to write that length down for every mutex in the system.

---

## Problem 3 — Stack budget for a hypothetical eight-task system

Design a stack budget for the following eight-task system on a Pi Pico W with 264 KB of SRAM, after the kernel reserves ~12 KB:

1. SysMon task (1 Hz)
2. Sensor read task (100 Hz, no printf)
3. Sensor read task (100 Hz, no printf)
4. Sensor fusion task (100 Hz)
5. Control loop (100 Hz, calls a 1500-byte locally-allocated buffer to a polynomial evaluator)
6. UART logger (10 Hz, calls printf with floats)
7. USB CDC (10 Hz, TinyUSB callback path)
8. Idle (implicit, hook with WFI)

Constraints:
- Every task must have a stack budget that can be justified by: peak measured usage + 64 words margin.
- `configMINIMAL_STACK_SIZE` is 128.
- The kernel's heap (`heap_4`) is 16 KB.

For each task, give:
- An initial guess for the stack size (in words).
- The peak measurement you would expect (in words).
- The shipping budget (peak + 64-word margin, rounded up).

Total RAM consumed by all eight stacks plus the heap should be < 30 KB so that the remaining ~220 KB is available for buffers, framebuffer, etc.

Deliverable: a `homework/p3-stack-budget.md` table.

---

## Problem 4 — Re-read the Mars Pathfinder case study

Read Mike Jones, "What really happened on Mars Rover Pathfinder" (1997), and answer four questions in one paragraph each:

(a) Which two tasks were involved in the priority inversion, and what was the resource they contended for?
(b) Why did the medium-priority task preempt the low-priority task?
(c) Which VxWorks mutex setting fixed the bug, and why was it disabled by default in the Pathfinder image?
(d) Translate the case into FreeRTOS terms: a `xSemaphoreCreateMutex` vs `xSemaphoreCreateBinary` choice. What is the FreeRTOS default — does it implement priority inheritance, or is it opt-in?

Reference: <https://www.cs.unc.edu/~anderson/teach/comp790/papers/mars_pathfinder_long_version.html>

Deliverable: `homework/p4-pathfinder.md`, 4 short paragraphs.

---

## Problem 5 — Bench task: implement a 100 Hz sensor task without `vTaskDelayUntil`

This is a *negative* exercise: deliberately write a 100 Hz task using `vTaskDelay(pdMS_TO_TICKS(10))`, and measure the actual rate by capturing a GPIO pulse the task emits on each iteration. Run for 1 minute. Compare the observed iteration count against 6000 (the expected count for 60 s × 100 Hz).

Then rewrite using `vTaskDelayUntil` and re-run. Compare again.

Deliverables (`homework/p5-vtaskdelay-drift/`):
- `version-a.c` (uses `vTaskDelay`)
- `version-b.c` (uses `vTaskDelayUntil`)
- `measurement-a.csv` (Saleae export, 60 s)
- `measurement-b.csv` (Saleae export, 60 s)
- `report.md` — the iteration counts you measured, the percentage drift, and a one-sentence explanation of why `vTaskDelayUntil` does not drift.

If the work inside the task body is "toggle a GPIO and post to a queue", the drift should be tiny (~0.1 %); make the task body do ~3 ms of `busy_wait_ms` to make the drift large and obvious.

---

## Problem 6 — Bench task: stack-overflow detection by deliberate overflow

Write a task that recursively calls itself with 32 bytes of local-array allocation per call. Start with `configCHECK_FOR_STACK_OVERFLOW = 2` and a stack of 256 words.

Run. Observe the kernel land in `vApplicationStackOverflowHook` after some N recursions.

Now disable the check (`configCHECK_FOR_STACK_OVERFLOW = 0`) and run the same overflow. Observe the system fail in a different way — typically a wild branch on a corrupted return address, often visible as garbage UART output or a `HardFault`.

Capture the boot UART log for both runs. The point: the check turns a silent corruption into a named diagnostic.

Deliverables (`homework/p6-stack-overflow/`):
- `overflow.c`
- `boot-log-with-check.txt` (the named `pcTaskName` in the hook)
- `boot-log-without-check.txt` (the garbled output)
- `report.md` — the recursion depth at which the check fired, and a one-sentence interpretation.

---

## Submission

Push the `homework/` subdirectory to your week-06 repo by Sunday 23:59 local. Six problems, ~3–4 hours total bench-and-writeup time. The four written problems are individually short; the two bench problems take an hour each including Saleae setup and reporting.
