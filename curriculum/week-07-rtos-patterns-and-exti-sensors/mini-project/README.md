# Mini-project — IMU EXTI Pipeline on FreeRTOS

The Week 7 deliverable. By Sunday 23:59 local you produce a public GitHub repo containing a Pi Pico W firmware that runs four FreeRTOS tasks — a 200 Hz MPU-6050 sensor ingest from EXTI, a 100 Hz attitude estimator, a 10 Hz downsampler, and a 1 Hz UART gatekeeper — communicating through one stream buffer, one length-1 overwrite queue, and one gatekeeper queue, with sub-5 µs measured 99-th-percentile ISR-to-task latency and analyzable worst-case blocking budget.

The point is to assemble every Week 7 primitive (deferred-interrupt, stream buffer, gatekeeper, priority-inheritance-aware design, `WFI`-idle power saving) into one coherent application and prove with measurement that the assembled system meets a documented timing contract.

---

## Architecture

```
   MPU-6050 INT  ----edge---->  GP22 IRQ  -- ISR (1.6 us) -+
                                                            |
                            ISR posts 14 bytes + 4-byte ts  |
                                                            v
                                              [mpu_stream  (256 B ring)]
                                                  trigger level: 14
                                                            |
                                                      blocking-read
                                                            |
                                                            v
                                               +-------------------+
                                               |  vIngest          |   prio 4
                                               |  - read stream    |
                                               |  - decode frame   |
                                               |  - push to estimator |
                                               +-------------------+
                                                            |
                                                  xQueueOverwrite
                                                            |
                                                            v
                                              [imu_latest_q  (length 1)]
                                                            |
                                                            v
                                               +-------------------+
                                               |  vEstimator       |   prio 3
                                               |  100 Hz wake      |
                                               |  - mahony fusion  |
                                               |  - emit quaternion|
                                               +-------------------+
                                                            |
                                                  xQueueOverwrite
                                                            |
                                                            v
                                              [quat_latest_q (length 1)]
                                                            |
                                                            v
                                               +-------------------+
                                               |  vDownsample      |   prio 2
                                               |  10 Hz wake       |
                                               |  - 10-sample avg  |
                                               |  - format string  |
                                               +-------------------+
                                                            |
                                                  xQueueSend (timeout=0)
                                                            |
                                                            v
                                              [uart_queue (length 8)]
                                                            |
                                                            v
                                               +-------------------+
                                               |  vGatekeeper      |   prio 1
                                               |  - dequeue        |
                                               |  - uart_puts      |
                                               +-------------------+
```

Four tasks, three queues, one stream buffer. No mutexes anywhere — the gatekeeper pattern eliminates the UART mutex, and the two `xQueueOverwrite`-style length-1 queues handle the latest-value handoffs without contention.

### Priority assignment (rate-monotonic)

| Task         | Wake source              | Rate         | Priority | Stack (words) |
|--------------|--------------------------|-------------|---------:|--------------:|
| vIngest      | stream buffer trigger    | 200 Hz       |        4 |           768 |
| vEstimator   | imu_latest_q              | 100 Hz       |        3 |          1024 |
| vDownsample  | quat_latest_q via vTaskDelayUntil | 10 Hz |        2 |           512 |
| vGatekeeper  | uart_queue                | event-driven |        1 |           768 |
| idle         | (kernel)                  | n/a          |        0 |           128 |

Rate-monotonic places vIngest at the top (200 Hz fastest), vEstimator next (100 Hz), vDownsample (10 Hz), vGatekeeper (1 Hz nominal). The kernel's idle task at 0.

### Stream-buffer sizing

- **mpu_stream**: 256 bytes, trigger level 14. Stores up to (256 / 18) = 14 frames of buffering, which at 200 Hz is 70 ms of headroom. The consumer (vIngest) runs every 5 ms; even a 20 ms hiccup leaves headroom.
- **imu_latest_q**: length 1, item size `sizeof(ImuSample_t)` = 32 bytes. Latest-value mailbox using `xQueueOverwrite`.
- **quat_latest_q**: length 1, item size `sizeof(Quaternion_t)` = 16 bytes. Latest-value mailbox.
- **uart_queue**: length 8, item size 96 bytes (a formatted line plus padding). 768 bytes of queue storage; on a 264 KB part this is invisible.

### Data types

```c
typedef struct {
    int16_t  accel_x, accel_y, accel_z;
    int16_t  temperature;
    int16_t  gyro_x, gyro_y, gyro_z;
    uint32_t timestamp_us;
    uint32_t isr_to_ingest_us;   /* end-to-end latency, populated by vIngest */
} ImuSample_t;  /* 24 bytes; round to 32 for alignment headroom */

typedef struct {
    float q0, q1, q2, q3;       /* unit quaternion */
} Quaternion_t;

typedef struct {
    char line[96];
} UartLine_t;
```

Field order is chosen so that 4-byte fields land on 4-byte boundaries (the kernel's memcpy is faster on aligned copies). `float` is used in the quaternion despite C7's general preference for fixed-point because the Cortex-M0+ has no FPU and the compiler will emit a software-float routine that is still fast enough at 100 Hz; fixed-point in this case adds complexity for no perceptible gain.

### Blocking budget

Each task's `B(T)` derived statically (the `BLOCKING-BUDGET.md` deliverable):

| Task        | M(T)                        | L(T) takers of M(T) | B(T) bound  |
|-------------|-----------------------------|---------------------|-------------|
| vIngest     | (none — uses queues)        | n/a                 | 0           |
| vEstimator  | (none)                      | n/a                 | 0           |
| vDownsample | (none)                      | n/a                 | 0           |
| vGatekeeper | (none)                      | n/a                 | 0           |

With zero mutexes in the system, B(T) = 0 for every task. The only source of inter-task delay is the queue receive — and queue receives are not "blocking on a resource the lower-priority task holds", they are "blocking on data the higher-priority task has not produced yet", which is fundamentally different and does not subject the receiver to priority inversion.

This is the analytical superpower of the gatekeeper-everywhere design: every task's worst-case latency is its own work plus the queue receive latency (single-digit microseconds). There is no inheritance chain to trace, no mutex hold to bound. The trade-off is the extra context switch per UART access, which is exactly what `LATENCY.md` will measure.

---

## Source layout

```
mini-project/
├── README.md              <-- you are here
├── CMakeLists.txt
├── FreeRTOSConfig.h
├── main.c                 <-- starter; pipeline wiring
├── mpu6050_driver.c       <-- you implement
├── mpu6050_driver.h       <-- starter; declarations
├── estimator.c            <-- you implement (Mahony attitude filter)
├── estimator.h
├── LATENCY.md             <-- you commit Saleae statistics
├── POWER.md               <-- you commit WFI-idle current measurement
├── BLOCKING-BUDGET.md     <-- you commit the per-task derivation
├── FAULT-MODEL.md         <-- you list the four ways this design can fail
└── saleae-captures/
    ├── isr-to-task-latency-30s.sal
    ├── uart-line-trace-60s.sal
    └── analysis.csv
```

The `mpu6050_driver.{c,h}` evolves your Exercise 2 code into a reusable driver. The `estimator.c` is a fresh implementation of the Mahony complementary filter (Mahony, Hamel, Pflimlin, 2008 — citation in the source comments); about 80 lines of float math. Both files are part of the deliverable.

---

## CMakeLists.txt (build recipe)

The canonical Pico-SDK + FreeRTOS-Kernel project skeleton, extended from Week 6.

```cmake
cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(week07_imu_exti C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# FreeRTOS-Kernel
set(FREERTOS_KERNEL_PATH $ENV{FREERTOS_KERNEL_PATH})
add_library(freertos_kernel STATIC
    ${FREERTOS_KERNEL_PATH}/tasks.c
    ${FREERTOS_KERNEL_PATH}/list.c
    ${FREERTOS_KERNEL_PATH}/queue.c
    ${FREERTOS_KERNEL_PATH}/stream_buffer.c
    ${FREERTOS_KERNEL_PATH}/timers.c
    ${FREERTOS_KERNEL_PATH}/portable/MemMang/heap_4.c
    ${FREERTOS_KERNEL_PATH}/portable/GCC/ARM_CM0/port.c
)
target_include_directories(freertos_kernel PUBLIC
    ${FREERTOS_KERNEL_PATH}/include
    ${FREERTOS_KERNEL_PATH}/portable/GCC/ARM_CM0
    ${CMAKE_CURRENT_LIST_DIR}    # for FreeRTOSConfig.h
)
target_link_libraries(freertos_kernel PUBLIC pico_stdlib)

add_executable(week07_imu_exti
    main.c
    mpu6050_driver.c
    estimator.c
)
target_link_libraries(week07_imu_exti
    pico_stdlib
    hardware_i2c
    hardware_gpio
    hardware_timer
    freertos_kernel
)
pico_add_extra_outputs(week07_imu_exti)
pico_enable_stdio_usb(week07_imu_exti 1)
pico_enable_stdio_uart(week07_imu_exti 0)
```

Note: STDIO is on USB (`stdio_usb`), not UART, because the UART is owned by the gatekeeper task and we do not want `printf` from a non-gatekeeper task to interleave on the wire. The gatekeeper writes to `uart0` directly via `uart_puts`.

---

## FreeRTOSConfig.h (key settings)

The Week 6 config plus:

```c
#define configUSE_STREAM_BUFFERS              1
#define configUSE_MESSAGE_BUFFERS             1
#define configUSE_TASK_NOTIFICATIONS          1   /* default; cite it anyway */
#define configMAX_PRIORITIES                  8
#define configCHECK_FOR_STACK_OVERFLOW        2
#define configUSE_IDLE_HOOK                   1   /* for __WFI */
#define configGENERATE_RUN_TIME_STATS         1   /* per-task CPU % */
#define configUSE_TRACE_FACILITY              1
#define configRECORD_STACK_HIGH_ADDRESS       1
```

The `vApplicationIdleHook` implementation:

```c
void vApplicationIdleHook(void)
{
    __wfi();    /* drop core power until next NVIC interrupt */
}
```

---

## Deliverables, in detail

### `LATENCY.md`

A 1-page markdown file containing:

- The Saleae capture file name (commit the `.sal` alongside).
- A statistics table from the Saleae's "Edges between two channels" view, measuring `GP21` (ISR-entry marker) rising to `GP20` (vIngest wake marker) rising.
- Capture length: 30 s minimum. Event count: 200 Hz × 30 s = 6 000 events.
- Required columns: median, p95, p99, max.
- Target: p99 ≤ 5 µs, max ≤ 10 µs.
- One paragraph explaining the tail (typically: `SysTick` interspersion at the median, kernel critical sections at the tail).

### `POWER.md`

A 1-page file containing:

- Multimeter or INA219 reading of the 3V3 supply current with the firmware running and the MPU-6050 at 200 Hz sampling.
- Same reading with the `vApplicationIdleHook` `__wfi();` line commented out and re-flashed.
- Expected: ~58 mA with `WFI`, ~85 mA without. Your numbers will vary ±5 mA depending on the Pico revision and the USB-vs-battery power source.
- One sentence justifying that the 27 mA savings is worth the < 5 cycles of added wake latency at 200 Hz.

### `BLOCKING-BUDGET.md`

The per-task `B(T)` derivation in the format from Lecture 3. With zero mutexes, your table will be trivially zeros. The deliverable instead is a paragraph per task explaining *why* it cannot block on a lower-priority task (typically: "uses queues, not mutexes; queue waits are on data the producer has not yet produced, which is not a priority-inversion path").

If you add a mutex for any reason (e.g. you decide to protect the `printf` floating-point context — `printf` is not reentrant by default), document it here and re-derive.

### `FAULT-MODEL.md`

A list of the four RTOS-specific failure modes this build defends against:

1. **Stack overflow** — `configCHECK_FOR_STACK_OVERFLOW = 2` plus `vApplicationStackOverflowHook` that logs the task name. Tested by deliberately under-sizing a task's stack and confirming the hook fires.
2. **Stream-buffer overrun (consumer too slow)** — ISR's send call returns short of the requested length; we increment `g_dropped_frames`. Logged by the gatekeeper every second. Tested by adding a `vTaskDelay(50 ms)` in vIngest and confirming drop count climbs.
3. **Queue full on uart_queue (gatekeeper too slow)** — vDownsample posts with `timeout = 0`; if the queue is full the line is dropped silently. Logged. Tested similarly.
4. **MPU INT line stuck high** — if the MPU enters an indeterminate state and asserts INT continuously, the ISR fires repeatedly with no useful data. We detect this by tracking the rate of ISR fires: if > 250 Hz sustained for 100 ms (vs the configured 200 Hz), we log a warning and re-init the MPU. Tested by intentionally writing 0xFF to INT_PIN_CFG (latched mode) and confirming detection.

Each defense gets its own paragraph: the failure mode, the detection mechanism, the recovery action (if any), and how you tested.

---

## Pass / fail criteria

The mini-project is "done" when all six are true:

1. `LATENCY.md` shows p99 ISR-to-task latency ≤ 5 µs and max ≤ 10 µs from a 30 s capture.
2. `POWER.md` shows ~30 % current reduction from the `__WFI` idle hook.
3. `BLOCKING-BUDGET.md` derives `B(T)` for all four tasks; the values are consistent with a one-hour measured peak.
4. `FAULT-MODEL.md` documents four failure modes with detection and tests.
5. The firmware runs for one hour on the bench with no missed UART line, no stack overflow, no dropped frame beyond 0.1 % of total.
6. The repo is on GitHub at a public URL with a clean `git log` (no debug commits left in).

You will demo the mini-project in a 10-minute slot during the Week 8 kickoff. The demo: power the Pico, open the Saleae, point at the latency capture in `LATENCY.md`, narrate the FAULT-MODEL.md, answer one question from the instructor about a specific design decision.

---

## Stretch goals

- Replace the binary-semaphore-style stream-buffer signalling with `xTaskNotifyGiveFromISR` to vIngest and a `xStreamBufferReceive` that does not block (poll the trigger). Measure the latency reduction. Document the trade-off in `NOTIFICATION-NOTE.md`.
- Switch from `heap_4` to `heap_5` and partition the FreeRTOS heap across two memory regions (e.g. one for kernel objects, one for task stacks). Verify with `vPortGetHeapStats` that the two regions are used as intended.
- Add a second EXTI source — a momentary button on `GP16` — that fires a "log dump" notification. The estimator task handles both the IMU sample and the button event via `xTaskNotifyWait` in bit-mask mode. Demonstrate that the notification bit-mask replaces what would have required two semaphores.
- Run the firmware with `configUSE_PREEMPTION = 0` (cooperative scheduling) and observe the latency trace. The p99 will become much worse (a low-priority task running a long path now blocks the entire system). Quantify and document.
- Port the firmware to a Pi Pico 2 (RP2350, ARM Cortex-M33) and re-measure. The M33 has 8 priority bits instead of 2; document how that changes the NVIC priority discussion in Lecture 1 and re-run Exercise 1 with the new latency numbers.

---

## Up next

[Week 8 — Drivers Inside the Kernel: DMA and PIO](../week-08/) — once this mini-project is on GitHub, the latency capture is in `LATENCY.md`, the power measurement is in `POWER.md`, and you can explain in one sentence why the gatekeeper-everywhere design has B(T) = 0 for every task.
