/*
 * Challenge 1 — Priority inversion: demonstrate, then fix
 *
 * The most instructive single artefact in Week 6.
 *
 * You will build a three-task program that reproduces the classic
 * priority-inversion failure, capture the failure on a logic analyzer,
 * then change exactly two characters (binary semaphore -> mutex) and
 * capture the fix. The before/after pair is your evidence; commit both
 * captures to the repo.
 *
 *   H — high priority   (prio 3) — needs a shared resource (the "bus")
 *   M — medium priority (prio 2) — does NOT need the resource; just runs
 *                                   CPU-bound work that preempts L
 *   L — low priority    (prio 1) — holds the resource for a long-ish time
 *
 * The failure scenario:
 *
 *   1. L acquires the resource and starts a 50 ms critical section.
 *   2. H wakes (e.g. a periodic deadline arrives) and tries to acquire the
 *      resource. It blocks.
 *   3. M wakes (also periodically) and runs for, say, 30 ms of CPU-bound
 *      work. M does not need the resource, so it does not block.
 *   4. With a *binary semaphore* guarding the resource: L is at priority 1,
 *      so M preempts L and runs to completion. L cannot resume until M
 *      finishes. H is starved through the entire M run, even though H is
 *      the highest-priority task. The end-to-end H latency is bounded by
 *      M's run time, not by L's critical section.
 *
 *   5. With a *mutex* guarding the resource: when H tries to take the mutex
 *      held by L, the priority-inheritance protocol promotes L to priority
 *      3 for the duration of the hold. M (still at priority 2) cannot
 *      preempt the now-promoted L. L finishes its critical section, gives
 *      the mutex, drops back to priority 1. H takes the mutex and runs.
 *      H's worst-case latency is now bounded by L's critical-section
 *      length, which is a knowable quantity.
 *
 * USE_MUTEX = 0  -> binary semaphore (broken; demonstrates the inversion)
 * USE_MUTEX = 1  -> mutex            (fixed)
 *
 * The "bus" is just a GPIO that the holder pulses for the duration of its
 * critical section. We don't actually transfer data — the timing is the
 * point. Use the GPIO trace, plus a per-task event-GPIO that pulses high
 * when each task runs, to visualize the schedule on a logic analyzer.
 *
 * Hardware:
 *   - Raspberry Pi Pico W.
 *   - Logic analyzer connected to:
 *       GP10 — H is running
 *       GP11 — M is running
 *       GP12 — L is running
 *       GP13 — "bus" held (set high when the resource is owned)
 *   - GND common.
 *
 * Verify on the bench:
 *
 *   USE_MUTEX = 0:
 *     Capture 1 second of all four lines. On the trace you will see L
 *     holding the bus (GP13 high), then M's GP11 pulses *during* the
 *     bus-held window — M is running while L cannot. H's GP10 wake is
 *     significantly delayed (typically 30-80 ms after H's "wake at" tick),
 *     proportional to M's run time.
 *
 *   USE_MUTEX = 1:
 *     Same capture but now M's GP11 pulses *outside* the bus-held window.
 *     L holds the bus, H tries to take, L is promoted, L finishes promptly,
 *     H takes immediately. H's wake-to-run latency drops to ~L_crit_section
 *     time plus a context switch — typically 50-55 ms total instead of
 *     80-130 ms.
 *
 * Reference for the protocol:
 *   Sha, Rajkumar, Lehoczky, "Priority Inheritance Protocols", IEEE Trans.
 *   Computers vol. 39 no. 9, Sep 1990.
 *   https://www.cs.cmu.edu/~rajkumar/papers/article_priority_inheritance.pdf
 *
 * Reference for the real-world incident:
 *   Mike Jones, "What really happened on Mars Rover Pathfinder", 1997.
 *   https://www.cs.unc.edu/~anderson/teach/comp790/papers/mars_pathfinder_long_version.html
 *
 * FreeRTOS API:
 *   xSemaphoreCreateBinary    — https://www.freertos.org/xSemaphoreCreateBinary.html
 *   xSemaphoreCreateMutex     — https://www.freertos.org/CreateMutex.html
 *   xSemaphoreTake            — https://www.freertos.org/a00122.html
 *   xSemaphoreGive            — https://www.freertos.org/a00123.html
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* Toggle this to switch between the broken and the fixed version.
 * Capture once with each. */
#ifndef USE_MUTEX
#define USE_MUTEX  1
#endif

/* ---- Pin assignments --------------------------------------------------- */

#define PIN_H_RUN     10u   /* high when H is running */
#define PIN_M_RUN     11u   /* high when M is running */
#define PIN_L_RUN     12u   /* high when L is running */
#define PIN_BUS_HELD  13u   /* high when the resource is owned */

/* ---- Task priorities (recall: higher number = higher priority) -------- */

#define PRIO_H        3u
#define PRIO_M        2u
#define PRIO_L        1u

#define STK_TASK      512u

/* ---- Periods (chosen to make the inversion visible) ------------------- */

#define H_PERIOD_MS   100u    /* H wakes every 100 ms, needs the bus */
#define M_PERIOD_MS   80u     /* M wakes every 80 ms, runs CPU-bound work */
#define L_PERIOD_MS   200u    /* L wakes every 200 ms, holds the bus 50 ms */

#define H_HOLD_MS     5u      /* H's critical section length */
#define M_BUSY_MS     30u     /* M's pure-CPU busy work */
#define L_HOLD_MS     50u     /* L's critical section length */

/* ---- Module state ----------------------------------------------------- */

static SemaphoreHandle_t xBusGuard = NULL;

/* ---- A spin-wait that respects the scheduler's wall-clock ------------- *
 *
 * busy_wait_ms_relative spins on xTaskGetTickCount, NOT on vTaskDelay.
 * This is intentional — it represents CPU-bound work, which is what makes
 * M dangerous to L. A vTaskDelay would block M, the scheduler would run
 * L, and the inversion would not be visible.
 *
 * In production you do NOT want unbounded busy-waits — they waste CPU.
 * For this demo, the busy-wait IS the workload being demonstrated.
 */
static void busy_wait_ms(uint32_t ms)
{
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(ms)) {
        /* spin — burn CPU intentionally */
    }
}

/* ---- Take/give the bus guard ------------------------------------------ *
 *
 * Same API for the binary semaphore and the mutex — only the create call
 * (and the kernel's internal bookkeeping) differs.
 */
static void bus_take(void)
{
    xSemaphoreTake(xBusGuard, portMAX_DELAY);
    gpio_put(PIN_BUS_HELD, 1);
}

static void bus_give(void)
{
    gpio_put(PIN_BUS_HELD, 0);
    xSemaphoreGive(xBusGuard);
}

/* ---- Task H — highest priority, needs the bus ------------------------ */
static void vTaskH(void *p)
{
    (void)p;
    const TickType_t period = pdMS_TO_TICKS(H_PERIOD_MS);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        gpio_put(PIN_H_RUN, 1);
        bus_take();
        /* This is the work whose latency we are measuring. */
        busy_wait_ms(H_HOLD_MS);
        bus_give();
        gpio_put(PIN_H_RUN, 0);
        vTaskDelayUntil(&last, period);
    }
}

/* ---- Task M — medium priority, CPU-bound, no bus -------------------- */
static void vTaskM(void *p)
{
    (void)p;
    const TickType_t period = pdMS_TO_TICKS(M_PERIOD_MS);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        gpio_put(PIN_M_RUN, 1);
        /* CPU-bound work — does not touch the bus. With a binary semaphore
         * guarding the bus, this work preempts L while L holds the bus,
         * causing H to wait through this entire run. */
        busy_wait_ms(M_BUSY_MS);
        gpio_put(PIN_M_RUN, 0);
        vTaskDelayUntil(&last, period);
    }
}

/* ---- Task L — lowest priority, holds the bus for a long time -------- */
static void vTaskL(void *p)
{
    (void)p;
    const TickType_t period = pdMS_TO_TICKS(L_PERIOD_MS);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        gpio_put(PIN_L_RUN, 1);
        bus_take();
        /* Long critical section — represents a slow flash write, a long
         * I2C transaction, anything that requires the bus for an extended
         * time. */
        busy_wait_ms(L_HOLD_MS);
        bus_give();
        gpio_put(PIN_L_RUN, 0);
        vTaskDelayUntil(&last, period);
    }
}

/* ---- Hooks ------------------------------------------------------------ */

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask; (void)pcTaskName;
    taskDISABLE_INTERRUPTS();
    for (;;) { /* spin */ }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) { /* spin */ }
}

/* ---- main -------------------------------------------------------------- */
int main(void)
{
    stdio_init_all();

    /* GPIO setup for the four scope lines. */
    const uint8_t pins[] = { PIN_H_RUN, PIN_M_RUN, PIN_L_RUN, PIN_BUS_HELD };
    for (size_t i = 0; i < sizeof(pins); i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 0);
    }

#if USE_MUTEX
    /* The fix: a mutex with priority inheritance. */
    xBusGuard = xSemaphoreCreateMutex();
#else
    /* The bug: a binary semaphore. Must be pre-given so the first take
     * succeeds. */
    xBusGuard = xSemaphoreCreateBinary();
    configASSERT(xBusGuard != NULL);
    xSemaphoreGive(xBusGuard);
#endif
    configASSERT(xBusGuard != NULL);

    BaseType_t rc;
    /* Create L first (lowest priority) so it has a chance to take the bus
     * before H wakes; otherwise the demo may not consistently reproduce. */
    rc = xTaskCreate(vTaskL, "L", STK_TASK, NULL, PRIO_L, NULL);
    configASSERT(rc == pdPASS);
    rc = xTaskCreate(vTaskM, "M", STK_TASK, NULL, PRIO_M, NULL);
    configASSERT(rc == pdPASS);
    rc = xTaskCreate(vTaskH, "H", STK_TASK, NULL, PRIO_H, NULL);
    configASSERT(rc == pdPASS);

    vTaskStartScheduler();
    for (;;) { /* spin */ }
    return 0;
}
