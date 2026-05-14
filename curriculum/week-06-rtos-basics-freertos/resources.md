# Week 6 — Resources

Every reference here is **free and publicly accessible**. FreeRTOS is MIT-licensed and the kernel sources, the API reference, and the long-form book ("Mastering the FreeRTOS Real Time Kernel") are all on freertos.org without registration. The RP2040 datasheet is a free PDF from Raspberry Pi. Where a page number is cited, the document revision is noted; later revisions move tables but not concepts.

## Primary FreeRTOS references

- **FreeRTOS Kernel — official site.** The kernel's documentation hub. Start here on every question:
  <https://www.freertos.org/>
- **FreeRTOS Kernel sources** (Amazon Web Services, MIT licensed, ~9 000 lines of portable C plus ~50 port directories). Clone this — you read it weekly:
  <https://github.com/FreeRTOS/FreeRTOS-Kernel>
  - `tasks.c` — the scheduler, `xTaskCreate`, `vTaskDelay`, the ready/blocked lists.
  - `queue.c` — queues, semaphores (binary, counting), mutexes — all on the same data structure with different bookkeeping.
  - `timers.c` — software timers (`xTimerCreate`); not in this week's scope but worth a skim.
  - `event_groups.c` — bitfield-based event groups; mentioned in Lecture 3 as the alternative to multi-semaphore patterns.
  - `portable/GCC/ARM_CM0/port.c` — the Cortex-M0+ port. About 400 lines. Contains the `PendSV` context-switch routine.
  - `portable/GCC/ARM_CM0/portmacro.h` — the port macros. Where `configASSERT`, `portYIELD_FROM_ISR`, and the disable-interrupt critical-section primitives live.
- **FreeRTOS API Reference** (full A–Z list, every function with parameters, return values, and the documented timing class):
  <https://www.freertos.org/a00106.html>
  - Task creation and control: <https://www.freertos.org/a00125.html> (`xTaskCreate`, `vTaskDelete`, `vTaskDelay`, `vTaskSuspend`, `vTaskResume`).
  - Queue management: <https://www.freertos.org/a00018.html> (`xQueueCreate`, `xQueueSend`, `xQueueReceive`, `xQueuePeek`, `xQueueOverwrite`, plus the `FromISR` variants).
  - Semaphore management: <https://www.freertos.org/a00113.html> (`xSemaphoreCreateBinary`, `xSemaphoreCreateCounting`, `xSemaphoreCreateMutex`, `xSemaphoreTake`, `xSemaphoreGive`, plus `FromISR` variants).
  - Mutex with priority inheritance: <https://www.freertos.org/CreateMutex.html>.
  - ISR API: <https://www.freertos.org/taskYIELD_FROM_ISR.html> and <https://www.freertos.org/a00124.html>.
- **"Mastering the FreeRTOS Real Time Kernel"** (Richard Barry, ~400 pages, free PDF from FreeRTOS.org). The canonical book. This week's chapters:
  - Chapter 3 — Tasks: task states, priorities, the idle task. The first hour of the week.
  - Chapter 4 — Queue Management: send, receive, peek, overwrite. Read end-to-end before Exercise 2.
  - Chapter 6 — Interrupt Management: the `FromISR` API, deferred interrupt handling, the "centralized deferred handling" pattern that Exercise 2 uses.
  - Chapter 7 — Resource Management: critical sections, scheduler suspension, mutexes, priority inversion, gatekeeper tasks. Required for Exercise 3 and Challenge 1.
  <https://www.freertos.org/Documentation/RTOS_book.html>
- **"FreeRTOS Reference Manual"** (Richard Barry, ~350 pages, free PDF, the A–Z companion to the book). Use as a desk reference when an API call's parameters or return semantics are unclear:
  <https://www.freertos.org/Documentation/RTOS_book.html>
- **`FreeRTOSConfig.h` reference** — every `config*` symbol, what it enables, what it costs, and what its default is:
  <https://www.freertos.org/a00110.html>
- **FreeRTOS supported MCU architectures** (the port list, ~50 architectures including ARM Cortex-M0+ used by the RP2040):
  <https://www.freertos.org/RTOS_ports.html>

## Raspberry Pi Pico / RP2040 references

- **RP2040 Datasheet** (Raspberry Pi Ltd, ~640 pages, Sep-2024 revision). Free PDF; the silicon reference for this week:
  <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>
  - §2.4 (Cortex-M0+), pp. 65–73 — the cores FreeRTOS runs on. Two of them; the SDK port pins the kernel to core 0 by default (SMP support exists but is gated behind `configNUMBER_OF_CORES > 1` and is out of scope for Week 6).
  - §2.4.1 (SysTick), pp. 96–98 — the 24-bit countdown timer FreeRTOS programs as its tick source. `configTICK_RATE_HZ` divides the system clock (125 MHz default) down to the tick rate.
  - §2.4.5 (NVIC), pp. 99–104 — the Nested Vectored Interrupt Controller. FreeRTOS uses `PendSV` (priority 0xFF, lowest, hardware-mandated for the context-switch trampoline) and `SVC` (priority 0xFF, used for the first-task launch).
  - §4.6 (TIMER), pp. 552–568 — the 64-bit timer at 1 MHz. Useful as a high-resolution stopwatch for `configGENERATE_RUN_TIME_STATS`; not used by the kernel itself.
  - §2.16 (Subsystem resets, SIO), pp. 36–40 — the inter-core SIO mailboxes. Not in scope for Week 6.
- **Raspberry Pi Pico C/C++ SDK documentation** (Raspberry Pi Ltd, browseable HTML, free):
  <https://www.raspberrypi.com/documentation/pico-sdk/>
  - `pico_stdlib`: the bring-up convenience library — `stdio_init_all`, `gpio_init`, etc.
  - `hardware_uart`: the UART driver — `uart_init`, `uart_set_baudrate`, `uart_putc`, `uart_puts`.
  - `hardware_i2c`: the I²C driver — `i2c_init`, `i2c_write_blocking`, `i2c_read_blocking`. Note "blocking" — these *are* safe to call from a task, *not* safe to call from an ISR.
  - `hardware_gpio`: GPIO config and interrupt routing — `gpio_set_irq_enabled`, `gpio_set_irq_callback`.
- **Raspberry Pi Pico SDK source** (BSD-3 licensed):
  <https://github.com/raspberrypi/pico-sdk>
- **Pico SDK + FreeRTOS integration example** (officially supported; the `CMakeLists.txt` patterns this week's mini-project follows):
  <https://github.com/raspberrypi/pico-examples/tree/master/freertos>
  (Note: at the time of writing, the `pico-examples` repo's `freertos/` directory contains a `hello_freertos` example that uses the SDK's `pico_cyw43_arch_freertos` wrapper. The mini-project's `CMakeLists.txt` is a simplified version that does not require `cyw43_arch` since we are not using Wi-Fi in Week 6.)

## ARM Cortex-M0+ references

- **ARMv6-M Architecture Reference Manual** (ARM DDI 0419D, ~480 pages, free). The Cortex-M0+ is an ARMv6-M part. The chapters that matter this week:
  - §B1.5 — Exceptions. `SysTick` is exception 15, `PendSV` is exception 14, `SVC` is exception 11. FreeRTOS uses all three.
  - §B3.3 — Interrupt priority. The M0+ has 4 priority levels (2 bits, top bits of an 8-bit field). FreeRTOS uses 0 (highest) for kernel-critical work and 3 (lowest) for `PendSV` and `SysTick`.
  - §B3.2.4 — The `PendSV` exception. The mechanism FreeRTOS uses for context switching: a low-priority interrupt that *pends* (so it runs after every other higher-priority ISR finishes), then performs the register-file save/restore in software.
  <https://developer.arm.com/documentation/ddi0419/d/>
- **ARM Cortex-M0+ Devices Generic User Guide** (ARM DUI 0662B, ~80 pages, free). A friendlier walkthrough of the same material. Read first if §B1.5 of the ARM is too dense:
  <https://developer.arm.com/documentation/dui0662/b/>

## Real-time scheduling theory (one-time read, not weekly)

- **Liu and Layland, "Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment"** (Journal of the ACM, vol. 20 no. 1, Jan 1973). The paper that introduced rate-monotonic scheduling and proved the worst-case utilization bound (`N · (2^(1/N) - 1)`, asymptotically ~69 %). About 15 pages. The result every embedded engineer should be able to state in one sentence:
  <https://dl.acm.org/doi/10.1145/321738.321743> (paywalled at ACM; the PDF circulates on author homepages — search "liu layland 1973 pdf").
- **Sha, Rajkumar, and Lehoczky, "Priority Inheritance Protocols: An Approach to Real-Time Synchronization"** (IEEE Transactions on Computers, vol. 39 no. 9, Sep 1990). The paper that introduced the priority-inheritance protocol FreeRTOS implements in its mutex. About 18 pages. Read at least the introduction, the example in §III, and the bound in §IV:
  <https://www.cs.cmu.edu/~rajkumar/papers/article_priority_inheritance.pdf> (free, hosted by CMU).
- **Mars Pathfinder priority-inversion case study.** The most famous real-world priority-inversion incident, on the Mars Pathfinder lander's VxWorks RTOS in 1997. The post-mortem ("What really happened on Mars?") by Mike Jones is a 4-page read and required for any RTOS engineer:
  <https://www.cs.unc.edu/~anderson/teach/comp790/papers/mars_pathfinder_long_version.html>
  (The lander reboot loop was caused by a low-priority meteorology task holding a VxWorks mutex on the inter-task pipe while a medium-priority comms task preempted it; the high-priority bus-management task missed its deadline and the watchdog rebooted the system. Fix: enable priority inheritance on the mutex. VxWorks supported it but Pathfinder's image had it disabled.)

## Static analyzers and runtime tools

- **`vApplicationStackOverflowHook`** — the FreeRTOS hook for the software stack-overflow check. Reference:
  <https://www.freertos.org/Stacks-and-stack-overflow-checking.html>
- **`uxTaskGetStackHighWaterMark`** — the runtime stack-usage query. Reference:
  <https://www.freertos.org/uxTaskGetStackHighWaterMark.html>
- **Tracealyzer** (Percepio AB, commercial, free 30-day eval). A GUI trace visualizer that ingests FreeRTOS's `vTrace*` hooks. Not required for Week 6 but useful for debugging the priority-inversion challenge:
  <https://percepio.com/tracealyzer/>
  (Free alternative: enable `configGENERATE_RUN_TIME_STATS` and print `vTaskGetRunTimeStats` output to UART — described in the README stretch goals.)
- **`pico-debug` / Picoprobe** — the official Raspberry Pi debug-probe firmware that turns a second Pico into a CMSIS-DAP probe. Useful when you want to set a breakpoint inside `vTaskSwitchContext` and watch the scheduler in action. Free, BSD-3:
  <https://github.com/raspberrypi/picoprobe>

## CMake and build references

- **Pico SDK CMake reference** — the macros (`pico_sdk_init`, `pico_add_extra_outputs`, `pico_enable_stdio_usb`, `pico_enable_stdio_uart`) you will see in this week's `CMakeLists.txt`:
  <https://www.raspberrypi.com/documentation/pico-sdk/cmake.html>
- **FreeRTOS-Kernel CMake support** (the kernel ships a `CMakeLists.txt` you `add_subdirectory(...)` into your project):
  <https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/CMakeLists.txt>

## Reading order for the week

1. Lecture 1 (this folder). 45 minutes.
2. "Mastering the FreeRTOS Real Time Kernel" Chapter 3 (Tasks). 60 minutes.
3. Lecture 2. 45 minutes.
4. Exercise 1 (two-task blink). 90 minutes including bench.
5. "Mastering the FreeRTOS Real Time Kernel" Chapter 4 (Queues). 60 minutes.
6. Lecture 3. 60 minutes.
7. Exercise 2 (button-to-LED through a queue). 90 minutes including bench.
8. "Mastering the FreeRTOS Real Time Kernel" Chapter 6 (Interrupts) and Chapter 7 (Resource Management). 90 minutes.
9. Exercise 3 (mutex around the UART). 90 minutes including bench.
10. Challenge 1 (priority inversion). 2 hours.
11. Mini-project. 6 hours across Saturday.
12. Quiz. 30 minutes.

The four-hour FreeRTOS book budget is non-negotiable for the week. The book is the only single text that covers every API call with worked C examples. The reference is for lookup; the book is for understanding.
