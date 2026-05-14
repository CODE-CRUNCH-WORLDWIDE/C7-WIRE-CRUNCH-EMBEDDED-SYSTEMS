/*
 * Exercise 3 — Priority-inheritance bound: derive, measure, compare
 *
 * Three tasks (H, M, L) at priorities 3, 2, 1. One mutex m. L takes m
 * and runs a 50 ms critical section. After 25 ms into L's critical
 * section, H wakes (a periodic timer) and tries to take m. H blocks;
 * priority inheritance promotes L to priority 3 for the remaining
 * 25 ms. M (priority 2) cannot preempt the promoted L.
 *
 * The derived bound: B(H) = max critical section L holds on m = 50 ms.
 * H's effective deadline-to-completion path under the worst-case
 * pre-emption is therefore (L's residual cs at the moment H wakes) +
 * (H's own work), bounded by 50 ms + H's work.
 *
 * In this exercise H's work is short (~50 us of GPIO toggles), so the
 * measured H-wake-to-H-complete time should be close to L's residual
 * critical section at the moment of H's wake. Because we wake H at a
 * deterministic offset (25 ms into L's cs), we expect:
 *
 *   measured peak B(H) ~= 25 ms     (under priority inheritance)
 *
 * Versus, with a binary semaphore instead of a mutex:
 *
 *   measured peak B(H) ~= 25 ms + (cumulative M run time during H's
 *                                  wait)
 *
 * where M's run time grows without bound across runs.
 *
 * Hardware:
 *   GP10 -- H is running              (Saleae channel 0)
 *   GP11 -- M is running              (Saleae channel 1)
 *   GP12 -- L is running and holding  (Saleae channel 2)
 *   GP13 -- H is blocked on the mutex (Saleae channel 3)
 *
 * Toggle USE_MUTEX between 0 and 1 to compare. Commit BOTH captures to
 * your homework directory (homework/p4-priority-bound-mutex.sal and
 * .../p4-priority-bound-semaphore.sal).
 *
 * Sha, Rajkumar, Lehoczky 1990 (IEEE TC 39:9): Theorem 3 derives the
 * bound for the basic priority-inheritance protocol. FreeRTOS mutexes
 * implement this protocol.
 */

#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ---- Configuration switch --------------------------------------------- */

/* 1 = use a FreeRTOS mutex (priority-inheritance enabled).
 * 0 = use a binary semaphore (no inheritance; demonstrates the broken
 *     case for comparison). */
#define USE_MUTEX           ((uint32_t)1u)

/* ---- Pin assignments --------------------------------------------------- */

#define PIN_H_RUN           ((uint32_t)10u)
#define PIN_M_RUN           ((uint32_t)11u)
#define PIN_L_RUN           ((uint32_t)12u)
#define PIN_H_BLOCKED       ((uint32_t)13u)

/* ---- Priorities -------------------------------------------------------- */

#define PRIO_H              ((UBaseType_t)3u)
#define PRIO_M              ((UBaseType_t)2u)
#define PRIO_L              ((UBaseType_t)1u)

/* ---- Stack sizes (words) ---------------------------------------------- */

#define STACK_DEFAULT       ((configSTACK_DEPTH_TYPE)512u)

/* ---- Timing parameters ------------------------------------------------ */

#define L_CRITICAL_MS       ((uint32_t)50u)   /* L's critical-section length */
#define H_PERIOD_MS         ((uint32_t)200u)  /* H's wake period */
#define H_WAKE_OFFSET_MS    ((uint32_t)25u)   /* delay before H's first wake */
#define M_PERIOD_MS         ((uint32_t)200u)  /* M's wake period */
#define M_WORK_MS           ((uint32_t)30u)   /* M's CPU-bound work */

/* ---- Globals ---------------------------------------------------------- */

static SemaphoreHandle_t g_guard = NULL;

/* ---- Busy-wait helper (uses 1 MHz timer; not vTaskDelay) -------------- */

/*
 * We need to occupy CPU time without yielding (vTaskDelay yields to the
 * scheduler). spin_us reads the 1 MHz TIMER peripheral and waits until
 * the requested microseconds have elapsed. Safe in any context.
 */
static void spin_us(uint32_t us)
{
    const uint32_t start = time_us_32();
    while ((time_us_32() - start) < us)
    {
        /* Volatile read makes the loop a real busy-wait that the
         * compiler cannot optimise out. */
        __asm volatile ("nop");
    }
}

/* ---- Task H ----------------------------------------------------------- */

static void vTaskH(void *pv)
{
    (void)pv;

    /* H is the highest-priority task. It wakes periodically, tries to
     * take the guard, does a tiny amount of work, releases the guard. */
    vTaskDelay(pdMS_TO_TICKS(H_WAKE_OFFSET_MS));

    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(H_PERIOD_MS);

    for (;;)
    {
        gpio_put(PIN_H_BLOCKED, 1);
        if (xSemaphoreTake(g_guard, portMAX_DELAY) == pdTRUE)
        {
            gpio_put(PIN_H_BLOCKED, 0);
            gpio_put(PIN_H_RUN, 1);

            /* H's "real work" - 50 us of busy CPU. */
            spin_us(50u);

            gpio_put(PIN_H_RUN, 0);
            (void)xSemaphoreGive(g_guard);
        }
        vTaskDelayUntil(&last, period);
    }
}

/* ---- Task M ----------------------------------------------------------- */

static void vTaskM(void *pv)
{
    (void)pv;

    /* M does NOT touch the guard. It just wakes periodically and runs
     * CPU-bound work that preempts L. */
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(M_PERIOD_MS);

    for (;;)
    {
        gpio_put(PIN_M_RUN, 1);
        spin_us(M_WORK_MS * 1000u);
        gpio_put(PIN_M_RUN, 0);
        vTaskDelayUntil(&last, period);
    }
}

/* ---- Task L ----------------------------------------------------------- */

static void vTaskL(void *pv)
{
    (void)pv;

    /* L takes the guard, holds it for L_CRITICAL_MS, releases.
     * Then sleeps for the rest of a 100 ms period so the system has
     * some idle time. */
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(100u);

    for (;;)
    {
        if (xSemaphoreTake(g_guard, portMAX_DELAY) == pdTRUE)
        {
            gpio_put(PIN_L_RUN, 1);
            spin_us(L_CRITICAL_MS * 1000u);
            gpio_put(PIN_L_RUN, 0);
            (void)xSemaphoreGive(g_guard);
        }
        vTaskDelayUntil(&last, period);
    }
}

/* ---- Setup ------------------------------------------------------------- */

static void configure_pins(void)
{
    const uint32_t pins[] = { PIN_H_RUN, PIN_M_RUN, PIN_L_RUN, PIN_H_BLOCKED };
    for (uint32_t i = 0u; i < (uint32_t)4u; ++i)
    {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 0);
    }
}

/* ---- main -------------------------------------------------------------- */

int main(void)
{
    stdio_init_all();
    configure_pins();

#if (USE_MUTEX == 1u)
    g_guard = xSemaphoreCreateMutex();
    printf("ex03: using MUTEX (priority inheritance on)\n");
#else
    g_guard = xSemaphoreCreateBinary();
    /* Binary semaphores are created in the "taken" state; give it once
     * so the first taker can proceed. */
    (void)xSemaphoreGive(g_guard);
    printf("ex03: using BINARY SEMAPHORE (no priority inheritance)\n");
#endif

    configASSERT(g_guard != NULL);

    BaseType_t rc;
    rc = xTaskCreate(vTaskH, "H", STACK_DEFAULT, NULL, PRIO_H, NULL);
    configASSERT(rc == pdPASS);
    rc = xTaskCreate(vTaskM, "M", STACK_DEFAULT, NULL, PRIO_M, NULL);
    configASSERT(rc == pdPASS);
    rc = xTaskCreate(vTaskL, "L", STACK_DEFAULT, NULL, PRIO_L, NULL);
    configASSERT(rc == pdPASS);

    printf("ex03: scheduler start\n");
    vTaskStartScheduler();

    for (;;)
    {
        tight_loop_contents();
    }
    return 0;
}
