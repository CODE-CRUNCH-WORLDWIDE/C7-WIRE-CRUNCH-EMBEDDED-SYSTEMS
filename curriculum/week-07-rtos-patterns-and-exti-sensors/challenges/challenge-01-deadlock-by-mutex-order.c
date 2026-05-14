/*
 * Challenge 1 — Deadlock by mutex order: reproduce, then fix
 *
 * The classical pathology of multi-mutex code. Two tasks each take two
 * mutexes in opposite orders. With unlucky timing, they wait for each
 * other forever. The kernel does not detect the deadlock; the scheduler
 * happily runs idle while two tasks are gone.
 *
 *   A: take m_alpha   -> ... -> take m_beta
 *   B: take m_beta    -> ... -> take m_alpha
 *
 * If A is preempted between its first and second take, and B runs in
 * the gap and reaches its second take, both tasks block forever.
 *
 * The fix: impose a global acquisition order. We use address order
 * (always take the mutex with the lower address first). Code review
 * enforces this; static analysers (Coverity, clang-analyzer
 * alpha.unix.Mutex) can catch violations.
 *
 *   USE_ORDER_FIX = 0  -> the buggy version. Deadlocks within seconds.
 *   USE_ORDER_FIX = 1  -> address-order fix. Runs indefinitely.
 *
 * Hardware:
 *   GP10 -- A is holding m_alpha
 *   GP11 -- A is holding m_beta
 *   GP12 -- B is holding m_alpha
 *   GP13 -- B is holding m_beta
 *   GP14 -- heartbeat (1 Hz from a third task)
 *
 * In the buggy version, the heartbeat keeps blinking but GP10..13 all
 * go (and stay) high in a pattern that never resolves. In the fixed
 * version, GP10..13 toggle rapidly and the heartbeat blinks normally.
 *
 * Commit two Saleae captures and a 1-page DEADLOCK-ANALYSIS.md.
 *
 * Sha-Rajkumar-Lehoczky 1990 is not strictly required for this case
 * (deadlock is a separate failure mode from priority inversion) but
 * the priority-ceiling protocol they introduce as an alternative
 * mitigates this scenario too (a task that would deadlock is blocked
 * earlier, before it takes any mutex).
 */

#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ---- Configuration switch --------------------------------------------- */

#define USE_ORDER_FIX       ((uint32_t)0u)   /* 0 = buggy, 1 = fixed */

/* ---- Pin assignments --------------------------------------------------- */

#define PIN_A_HAS_ALPHA     ((uint32_t)10u)
#define PIN_A_HAS_BETA      ((uint32_t)11u)
#define PIN_B_HAS_ALPHA     ((uint32_t)12u)
#define PIN_B_HAS_BETA      ((uint32_t)13u)
#define PIN_HEARTBEAT       ((uint32_t)14u)

/* ---- Priorities -------------------------------------------------------- */

#define PRIO_AB             ((UBaseType_t)2u)
#define PRIO_HEARTBEAT      ((UBaseType_t)1u)

/* ---- Stack sizes (words) ---------------------------------------------- */

#define STACK_DEFAULT       ((configSTACK_DEPTH_TYPE)512u)

/* ---- Globals ---------------------------------------------------------- */

static SemaphoreHandle_t g_m_alpha = NULL;
static SemaphoreHandle_t g_m_beta  = NULL;

/* For the address-order fix we sort the two mutexes by handle address
 * at creation. ordered[0] is the lower address. The 'first' and 'second'
 * names below refer to acquisition order, not the original alpha/beta
 * identity. */
static SemaphoreHandle_t g_ordered_first  = NULL;
static SemaphoreHandle_t g_ordered_second = NULL;

/* ---- Helpers ---------------------------------------------------------- */

static void busy_work_us(uint32_t us)
{
    /* Crude busy wait; the kernel does not yield in this loop. */
    volatile uint32_t i;
    const uint32_t loops = (us * 25u);   /* ~25 cycles per loop at 125 MHz */
    for (i = 0u; i < loops; ++i)
    {
        __asm volatile ("nop");
    }
}

/* ---- Task A ----------------------------------------------------------- */

static void vTaskA(void *pv)
{
    (void)pv;

    for (;;)
    {
#if (USE_ORDER_FIX == 1u)
        /* Address-ordered acquisition. Both A and B take ordered_first
         * before ordered_second. Deadlock impossible. */
        if (xSemaphoreTake(g_ordered_first, portMAX_DELAY) == pdTRUE)
        {
            gpio_put((g_ordered_first == g_m_alpha) ? PIN_A_HAS_ALPHA
                                                   : PIN_A_HAS_BETA, 1);
            busy_work_us(500u);

            if (xSemaphoreTake(g_ordered_second, portMAX_DELAY) == pdTRUE)
            {
                gpio_put((g_ordered_second == g_m_alpha) ? PIN_A_HAS_ALPHA
                                                        : PIN_A_HAS_BETA, 1);
                busy_work_us(500u);
                gpio_put((g_ordered_second == g_m_alpha) ? PIN_A_HAS_ALPHA
                                                        : PIN_A_HAS_BETA, 0);
                (void)xSemaphoreGive(g_ordered_second);
            }

            gpio_put((g_ordered_first == g_m_alpha) ? PIN_A_HAS_ALPHA
                                                   : PIN_A_HAS_BETA, 0);
            (void)xSemaphoreGive(g_ordered_first);
        }
#else
        /* Buggy: A always takes alpha first, then beta. */
        if (xSemaphoreTake(g_m_alpha, portMAX_DELAY) == pdTRUE)
        {
            gpio_put(PIN_A_HAS_ALPHA, 1);
            busy_work_us(500u);   /* enough for B to run and grab beta */

            if (xSemaphoreTake(g_m_beta, portMAX_DELAY) == pdTRUE)
            {
                gpio_put(PIN_A_HAS_BETA, 1);
                busy_work_us(200u);
                gpio_put(PIN_A_HAS_BETA, 0);
                (void)xSemaphoreGive(g_m_beta);
            }

            gpio_put(PIN_A_HAS_ALPHA, 0);
            (void)xSemaphoreGive(g_m_alpha);
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(5u));
    }
}

/* ---- Task B ----------------------------------------------------------- */

static void vTaskB(void *pv)
{
    (void)pv;

    for (;;)
    {
#if (USE_ORDER_FIX == 1u)
        /* Same global order as A. */
        if (xSemaphoreTake(g_ordered_first, portMAX_DELAY) == pdTRUE)
        {
            gpio_put((g_ordered_first == g_m_alpha) ? PIN_B_HAS_ALPHA
                                                   : PIN_B_HAS_BETA, 1);
            busy_work_us(500u);

            if (xSemaphoreTake(g_ordered_second, portMAX_DELAY) == pdTRUE)
            {
                gpio_put((g_ordered_second == g_m_alpha) ? PIN_B_HAS_ALPHA
                                                        : PIN_B_HAS_BETA, 1);
                busy_work_us(500u);
                gpio_put((g_ordered_second == g_m_alpha) ? PIN_B_HAS_ALPHA
                                                        : PIN_B_HAS_BETA, 0);
                (void)xSemaphoreGive(g_ordered_second);
            }

            gpio_put((g_ordered_first == g_m_alpha) ? PIN_B_HAS_ALPHA
                                                   : PIN_B_HAS_BETA, 0);
            (void)xSemaphoreGive(g_ordered_first);
        }
#else
        /* Buggy: B always takes beta first, then alpha. */
        if (xSemaphoreTake(g_m_beta, portMAX_DELAY) == pdTRUE)
        {
            gpio_put(PIN_B_HAS_BETA, 1);
            busy_work_us(500u);   /* race window with A's mid-section */

            if (xSemaphoreTake(g_m_alpha, portMAX_DELAY) == pdTRUE)
            {
                gpio_put(PIN_B_HAS_ALPHA, 1);
                busy_work_us(200u);
                gpio_put(PIN_B_HAS_ALPHA, 0);
                (void)xSemaphoreGive(g_m_alpha);
            }

            gpio_put(PIN_B_HAS_BETA, 0);
            (void)xSemaphoreGive(g_m_beta);
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(5u));
    }
}

/* ---- Heartbeat task (proves the kernel is still alive even when AB are deadlocked) */

static void vHeartbeatTask(void *pv)
{
    (void)pv;

    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(500u);

    for (;;)
    {
        gpio_put(PIN_HEARTBEAT, !gpio_get(PIN_HEARTBEAT));
        vTaskDelayUntil(&last, period);
    }
}

/* ---- Setup ------------------------------------------------------------- */

static void configure_pins(void)
{
    const uint32_t pins[] = {
        PIN_A_HAS_ALPHA, PIN_A_HAS_BETA,
        PIN_B_HAS_ALPHA, PIN_B_HAS_BETA,
        PIN_HEARTBEAT
    };
    for (uint32_t i = 0u; i < (uint32_t)5u; ++i)
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

    g_m_alpha = xSemaphoreCreateMutex();
    g_m_beta  = xSemaphoreCreateMutex();
    configASSERT(g_m_alpha != NULL);
    configASSERT(g_m_beta  != NULL);

    /* Sort by handle address. The lower-address mutex is acquired
     * first by both tasks under the fix. */
    if ((uintptr_t)g_m_alpha < (uintptr_t)g_m_beta)
    {
        g_ordered_first  = g_m_alpha;
        g_ordered_second = g_m_beta;
    }
    else
    {
        g_ordered_first  = g_m_beta;
        g_ordered_second = g_m_alpha;
    }

#if (USE_ORDER_FIX == 1u)
    printf("ch01: address-order fix ENABLED\n");
#else
    printf("ch01: BUGGY (no order); expect deadlock within seconds\n");
#endif

    BaseType_t rc;
    rc = xTaskCreate(vTaskA,         "A", STACK_DEFAULT, NULL, PRIO_AB,
                     NULL);
    configASSERT(rc == pdPASS);
    rc = xTaskCreate(vTaskB,         "B", STACK_DEFAULT, NULL, PRIO_AB,
                     NULL);
    configASSERT(rc == pdPASS);
    rc = xTaskCreate(vHeartbeatTask, "hb", STACK_DEFAULT, NULL,
                     PRIO_HEARTBEAT, NULL);
    configASSERT(rc == pdPASS);

    vTaskStartScheduler();

    for (;;)
    {
        tight_loop_contents();
    }
    return 0;
}
