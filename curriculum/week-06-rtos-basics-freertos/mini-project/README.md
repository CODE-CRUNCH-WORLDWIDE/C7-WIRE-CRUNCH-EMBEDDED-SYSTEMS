# Mini-project — Three-task sensor pipeline on FreeRTOS

The Week 6 deliverable. By Sunday 23:59 local you produce a public GitHub repo containing a Pi Pico W firmware that runs three FreeRTOS tasks — a 10 Hz BMP280 sensor read, a 10 Hz fixed-point Celsius transform, and a 1 Hz UART print — communicating through one queue and protected by one mutex. The system runs for an hour without a missed UART line, a stack overflow, or a queue overrun.

The point is to assemble every primitive from the three exercises into a single coherent application and prove, with measurement, that the assembled system meets a documented timing contract.

---

## Architecture

```
   +-------------+       sensor_q          +---------------+    uart_mutex   +-------------+
   |  vSensor    | -- xQueueSend -------> |  vTransform   | -- take/give -> |   UART0     |
   |  prio 3     |    (4-element FIFO     |  prio 2       |                  |   115200    |
   |  10 Hz      |     of Reading_t)      |  10 Hz        |                  |    8N1      |
   +-------------+                         +---------------+                  +-------------+
        |                                         |                                ^
        |  i2c0 to BMP280                         |  posts formatted line          |
        v                                         v                                |
   +-------------+                         +---------------+                       |
   |   BMP280    |                         |   vPrinter    | --------- take/give --+
   |    0x76     |                         |   prio 1      |
   +-------------+                         |   1 Hz        |
                                            +---------------+
```

Three tasks, one queue, one mutex. Note: the diagram shows the transform task posting to the UART under the mutex, *but* in this design the transform writes its result into a *latest-value mailbox* (a length-1 queue with `xQueueOverwrite`) and the printer reads it at 1 Hz. This decouples the transform's 10 Hz rate from the printer's 1 Hz rate, and avoids posting to the UART from two different tasks.

A cleaner production design uses a *gatekeeper* (Lecture 3) — a single task owns the UART, and every other task posts strings to its queue. The mini-project uses a mutex for clarity; the stretch goal asks you to rewrite using a gatekeeper.

### Priority assignment (rate-monotonic)

| Task        | Rate    | Priority | Stack (words) | Justification                                  |
|-------------|---------|---------:|--------------:|------------------------------------------------|
| `vSensor`   | 10 Hz   |        3 |           512 | I2C blocking, short critical sections           |
| `vTransform`| 10 Hz   |        2 |           384 | Fixed-point math, no I/O                        |
| `vPrinter`  | 1 Hz    |        1 |          1024 | printf via pico-sdk; printf is stack-heavy      |
| `idle`      | (impl.) |        0 |           256 | idle hook with WFI                              |

Sensor and transform are at the same rate; the sensor is given the higher priority because it has the tighter deadline (sensor read → queue post must complete before the transform's consume to keep the queue from starving).

### Queue and mutex sizing

- **`sensor_q`**: length 4, item size `sizeof(SensorReading_t)` (12 bytes). Length 4 means up to 400 ms of buffered readings; longer than that and the transform is fatally behind. In practice the queue never has more than 1 item at a time because the consumer is at the same rate as the producer.
- **`latest_q`**: length 1, item size `sizeof(CelsiusReading_t)` (12 bytes). `xQueueOverwrite`-style mailbox.
- **`uart_mutex`**: `xSemaphoreCreateMutex`. Priority inheritance enabled by default.

### Data types

```c
typedef struct {
    uint32_t raw;            /* raw BMP280 temperature register, 20 bits */
    uint32_t timestamp_tick; /* tick at which the read started */
    int32_t  bmp_status;     /* 0 = OK, < 0 = I2C / device error */
} SensorReading_t;

typedef struct {
    int32_t  celsius_q8;     /* temperature in Q24.8 fixed point (1/256 C) */
    uint32_t timestamp_tick; /* propagated from the SensorReading_t */
    int32_t  status;
} CelsiusReading_t;
```

12 bytes each. Choose the field order to align well (4-byte boundaries) so the kernel's `memcpy`-into-queue path is fast.

---

## Source layout

```
mini-project/
├── README.md              <-- you are here
├── CMakeLists.txt
├── FreeRTOSConfig.h
├── main.c
├── bmp280_driver.c
├── bmp280_driver.h
├── stack_budget.md         <-- HWM measurements you commit
├── fault_model.md          <-- the four RTOS-specific failure modes you defend
└── saleae-captures/
    ├── uart-30s-clean.sal
    └── analysis.csv
```

The `bmp280_driver.{c,h}` is your Week 4 driver, reused unchanged. The mini-project does NOT rewrite the driver; it wraps the existing `bmp280_read_raw()` in a FreeRTOS task and lets the rest of the system flow through queues and mutexes.

---

## CMakeLists.txt (build recipe)

This is the canonical Pico-SDK + FreeRTOS-Kernel project skeleton. Adjust paths if your `PICO_SDK_PATH` and `FREERTOS_KERNEL_PATH` are non-standard.

```cmake
cmake_minimum_required(VERSION 3.13)

# --- Pico SDK bootstrap (must come first) -------------------------------
# Expects PICO_SDK_PATH in the environment, pointing at a checkout of
# https://github.com/raspberrypi/pico-sdk
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# --- FreeRTOS bootstrap (must come BEFORE project()) -------------------
# Expects FREERTOS_KERNEL_PATH in the environment, pointing at a checkout
# of https://github.com/FreeRTOS/FreeRTOS-Kernel
# The kernel ship a CMake helper file for the SDK port.
include($ENV{FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

project(crunch_wire_w6_mini C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(${PROJECT_NAME}
    main.c
    bmp280_driver.c
)

# Add our FreeRTOSConfig.h to the include path for the kernel.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pico_stdlib
    hardware_i2c
    hardware_uart
    hardware_gpio
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4    # heap_4 allocator (coalescing, default choice)
)

# Stdio over UART0 by default; disable USB-CDC stdio so the UART trace is
# clean for the Saleae capture.
pico_enable_stdio_uart(${PROJECT_NAME} 1)
pico_enable_stdio_usb(${PROJECT_NAME}  0)

pico_add_extra_outputs(${PROJECT_NAME})  # generates .uf2, .bin, .hex

# Build options: warnings as errors, size optimization, gc-sections.
target_compile_options(${PROJECT_NAME} PRIVATE
    -Os
    -Wall
    -Wextra
    -Werror
    -ffunction-sections
    -fdata-sections
)

target_link_options(${PROJECT_NAME} PRIVATE
    -Wl,--gc-sections
    -Wl,--print-memory-usage    # prints the .text / .data / .bss sizes at link time
)
```

This is the minimal viable build. The two `include(...)` lines before `project()` are mandatory — the SDK and FreeRTOS bootstraps must run before CMake's project initialization.

The `--print-memory-usage` linker flag prints a summary like:

```
Memory region         Used Size  Region Size  %age Used
           FLASH:       38112 B         2 MB        1.82%
             RAM:        6432 B       256 KB        2.45%
       SCRATCH_X:           0 B         4 KB        0.00%
       SCRATCH_Y:           0 B         4 KB        0.00%
```

The kernel + your three tasks + the I2C driver + stdio should fit in well under 50 KB of flash and 12 KB of RAM. If you exceed 32 KB of RAM, your stacks are too large — measure with `uxTaskGetStackHighWaterMark` and re-size.

---

## FreeRTOSConfig.h

The Week 6 reference configuration. Copy verbatim into your `mini-project/FreeRTOSConfig.h`:

```c
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ---- Scheduler --------------------------------------------------------- */
#define configUSE_PREEMPTION            1
#define configUSE_TIME_SLICING          1
#define configMAX_PRIORITIES            8
#define configTICK_RATE_HZ              ((TickType_t)1000)
#define configMINIMAL_STACK_SIZE        ((uint16_t)256)
#define configMAX_TASK_NAME_LEN         16
#define configUSE_16_BIT_TICKS          0
#define configIDLE_SHOULD_YIELD         1

/* ---- Memory ------------------------------------------------------------ */
#define configSUPPORT_STATIC_ALLOCATION 0
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configTOTAL_HEAP_SIZE           (16 * 1024)

/* ---- Hooks ------------------------------------------------------------- */
#define configUSE_IDLE_HOOK             1
#define configUSE_TICK_HOOK             0
#define configUSE_MALLOC_FAILED_HOOK    1
#define configCHECK_FOR_STACK_OVERFLOW  2

/* ---- Synchronization --------------------------------------------------- */
#define configUSE_MUTEXES               1
#define configUSE_RECURSIVE_MUTEXES     0
#define configUSE_COUNTING_SEMAPHORES   1
#define configQUEUE_REGISTRY_SIZE       8

/* ---- Diagnostics ------------------------------------------------------- */
#define configUSE_TRACE_FACILITY        1
#define configGENERATE_RUN_TIME_STATS   0
#define configRECORD_STACK_HIGH_ADDRESS 1
#define configCHECK_FOR_STACK_OVERFLOW  2

/* ---- API features ------------------------------------------------------ */
#define INCLUDE_vTaskPrioritySet        1
#define INCLUDE_uxTaskPriorityGet       1
#define INCLUDE_vTaskDelete             1
#define INCLUDE_vTaskSuspend            1
#define INCLUDE_xTaskGetSchedulerState  1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_vTaskDelay              1
#define INCLUDE_vTaskDelayUntil         1

/* ---- Asserts (development build) -------------------------------------- */
extern void vAssertCalled(const char *file, int line);
#define configASSERT( x ) if ((x) == 0) vAssertCalled(__FILE__, __LINE__)

/* ---- Cortex-M0+ interrupt priority configuration ---------------------- */
/* M0+ has 4 priority levels (top 2 bits of an 8-bit field). FreeRTOS uses
 * the lowest priority (3) for SysTick and PendSV, and the highest (0) for
 * critical-section masking. */
#define configKERNEL_INTERRUPT_PRIORITY        (255)
#define configMAX_SYSCALL_INTERRUPT_PRIORITY   (191)

#endif /* FREERTOS_CONFIG_H */
```

---

## The `main.c` skeleton

The full source is yours to write. The structure should match this skeleton — you fill in the bodies:

```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "bmp280_driver.h"

/* ---- Configuration ----------------------------------------------------- */
#define I2C_INSTANCE     i2c0
#define I2C_SDA_PIN      4u
#define I2C_SCL_PIN      5u
#define I2C_FREQ_HZ      100000u

#define UART_INSTANCE    uart0
#define UART_BAUD        115200u
#define UART_TX_PIN      0u
#define UART_RX_PIN      1u

#define BMP280_ADDR      0x76u

/* ---- Globals ----------------------------------------------------------- */
static QueueHandle_t     sensor_q;
static QueueHandle_t     latest_q;
static SemaphoreHandle_t uart_mutex;

/* ---- Data types --- see above ----------------------------------------- */

/* ---- Tasks ------------------------------------------------------------- */
static void vSensorTask(void *pv);     /* 10 Hz, reads BMP280, sends to sensor_q */
static void vTransformTask(void *pv);  /* 10 Hz, drains sensor_q, overwrites latest_q */
static void vPrinterTask(void *pv);    /* 1 Hz, peeks latest_q, prints via uart_mutex */

/* ---- Hooks ------------------------------------------------------------- */
void vApplicationIdleHook(void);
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);
void vApplicationMallocFailedHook(void);
void vAssertCalled(const char *file, int line);

/* ---- main -------------------------------------------------------------- */
int main(void) {
    /* 1. Hardware init: I2C, UART, GPIO pin functions. */
    /* 2. bmp280_init(I2C_INSTANCE, BMP280_ADDR); */
    /* 3. Create queues and the mutex. configASSERT non-NULL. */
    /* 4. Create three tasks with priorities 3 / 2 / 1. */
    /* 5. vTaskStartScheduler(); */
    /* 6. for (;;) {} */
}
```

The expected length of `main.c` is ~250 lines including the three task bodies and the four hooks. Keep it short — the discipline is in `bmp280_driver.c` (which you already wrote in Week 4) and in `FreeRTOSConfig.h`.

---

## Verification — what "done" looks like

Five concrete pass/fail criteria. All five must pass before you tag the repo as the Week 6 deliverable.

### 1. UART trace is clean for 30 seconds

Capture UART0 on a Saleae for 30 seconds. Decode as Async Serial 115200 8N1. Export as CSV. Every line should match the format:

```
[t=12345] temp = 24.328 C  (q8 = 6228)
```

Verify with `awk` that the format is consistent on every line:

```sh
awk -F',' '{ print $5 }' analysis.csv | grep -v '^\[t=[0-9]\+\] temp = [-0-9.]\+ C' | wc -l
```

Should return 0.

### 2. Stack budgets are documented

Run with all four `uxTaskGetStackHighWaterMark` measurements logged once per 5 seconds. Commit a `stack_budget.md` table:

| Task       | Configured | Peak usage | Free margin |
|------------|-----------:|-----------:|------------:|
| sensor     |       512  |       ~210 |        ~302 |
| transform  |       384  |       ~180 |        ~204 |
| printer    |      1024  |       ~860 |        ~164 |
| idle       |       256  |       ~120 |        ~136 |

Margin must be ≥ 64 words for every task. If less, increase the configured size and re-test.

### 3. No queue or mutex failures

Add counters: `s_queue_full`, `s_mutex_timeout`. Print them in the periodic UART line. After 30 minutes, both counters must remain 0. If either is non-zero, your sizing is wrong (queue too short, mutex held too long) and you must fix it before shipping.

### 4. `FAULT-MODEL.md` documents the four RTOS-specific failure modes

```
1. Stack overflow      -> caught by configCHECK_FOR_STACK_OVERFLOW = 2 + hook.
2. Queue overrun       -> caught by counter; producer drops; logger reports.
3. Mutex deadlock      -> "take-without-give" caught by 100 ms timeout on take.
4. Priority inversion  -> impossible by construction: only one mutex; mutex is
                          a FreeRTOS mutex (with PI), not a binary semaphore.
```

This is a 1-page Markdown file you ship with the repo.

### 5. CMake build is reproducible from a clean checkout

A reviewer should be able to:

```sh
git clone <your-repo>
cd <your-repo>/mini-project
mkdir build && cd build
cmake -DPICO_SDK_PATH=$PICO_SDK_PATH -DFREERTOS_KERNEL_PATH=$FREERTOS_KERNEL_PATH ..
make -j
```

and produce a `crunch_wire_w6_mini.uf2`. No paths hardcoded in the source. The repo's top-level README documents the build steps.

---

## Stretch goals

- **Gatekeeper UART.** Replace the `uart_mutex` with a dedicated `vUartTask` at priority 2 that owns the UART. Other tasks `xQueueSend` formatted lines into a `uart_q`; the gatekeeper drains and writes. Compare: the stack-budget impact of the gatekeeper, the `uart_q` overhead vs the mutex, and the cleanness of the code path. Decide which design you prefer and write a one-paragraph note in the repo.
- **`vTaskGetRunTimeStats`.** Enable `configGENERATE_RUN_TIME_STATS = 1`. Supply the two macros referencing the RP2040 TIMER peripheral (datasheet §4.6, pp. 552–568). Call `vTaskGetRunTimeStats(buf)` from the printer task once every 10 seconds and print the per-task CPU percentages. Expected: sensor and transform each < 5 %, printer < 1 %, idle > 90 %.
- **Idle hook with `WFI`.** Implement `vApplicationIdleHook` to `__asm volatile("wfi")`. Measure system current draw with a multimeter before and after. Expect a drop from ~20 mA to ~3–5 mA.
- **EXTI-driven sensor read.** Replace the polled 10 Hz sensor task with an interrupt-driven one: configure the BMP280 to assert its INT pin when a new reading is ready, route that to a Pico GPIO interrupt, and have the ISR `xSemaphoreGiveFromISR` to wake a task. Measure the latency from sensor-ready to task-resume on a logic analyzer. (This is the Week 7 mini-project warm-up.)
- **Static allocation.** Set `configSUPPORT_STATIC_ALLOCATION = 1`, allocate every task and queue with the `Static` variants (`xTaskCreateStatic`, `xQueueCreateStatic`, `xSemaphoreCreateMutexStatic`), and set `configTOTAL_HEAP_SIZE = 0`. The system now has no heap at all — every allocation is a statically-sized buffer at compile time. Verify the `.elf`'s RAM section is unchanged (the buffers moved from heap to BSS).

---

## Submission checklist

- [ ] Public GitHub repo created.
- [ ] `mini-project/main.c`, `bmp280_driver.{c,h}`, `CMakeLists.txt`, `FreeRTOSConfig.h` committed.
- [ ] `mini-project/stack_budget.md` with measured HWMs.
- [ ] `mini-project/fault_model.md` covering the four failure modes.
- [ ] `mini-project/saleae-captures/uart-30s-clean.sal` and the matching `analysis.csv`.
- [ ] Top-level `README.md` documenting build steps and `PICO_SDK_PATH` / `FREERTOS_KERNEL_PATH` expectations.
- [ ] Tag `v0.6.0` on the merge commit.
- [ ] Course staff have the repo URL by Sunday 23:59 local.

Good luck. The kernel will not be on your side until you have measured its behaviour on your own bench. Build, measure, document, repeat.
