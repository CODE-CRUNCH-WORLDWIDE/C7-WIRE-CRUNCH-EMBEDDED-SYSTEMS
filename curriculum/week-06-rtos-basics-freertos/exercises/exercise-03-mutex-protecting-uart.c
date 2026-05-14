/*
 * Exercise 3 — Two tasks share UART through a mutex
 *
 * Goal: demonstrate resource protection with a mutex. Two tasks both
 * print to UART0 at different rates. Without protection their output
 * interleaves byte-by-byte and the receiver sees garbled lines. With a
 * mutex around the print, each task's line lands on the wire whole.
 *
 * Run the program twice:
 *   1. With WITH_MUTEX = 0 (the racy version) — capture the UART on a
 *      logic analyzer and decode. Lines from the two tasks interleave;
 *      every few seconds you see something like:
 *         "[fast 12<slow 7>][fast 13][fast 14]"
 *      or worse, mid-byte interleaving where a UART receiver loses
 *      framing.
 *   2. With WITH_MUTEX = 1 (the corrected version) — capture again.
 *      Lines are now whole; "[fast 12]\r\n[slow 7]\r\n[fast 13]\r\n..."
 *      etc., in order, without truncation.
 *
 * The point of the exercise is the *measurement*. The fact that a mutex
 * prevents interleaving is theoretical until you have the two logic-analyzer
 * captures side by side. Save both as CSVs and check them in to your
 * exercise repo.
 *
 * Hardware:
 *   - Raspberry Pi Pico W.
 *   - UART0 TX on GP0 (pin 1). Connect to a USB-serial adapter at
 *     115200 8N1. Saleae Logic 8 can decode the line in software; a
 *     USB-CDC bridge to a terminal (minicom, picocom, screen) is also
 *     fine for a visual read.
 *
 * Verify on the bench:
 *   - WITH_MUTEX = 0: lines visibly interleave under the load. Logic
 *     analyzer shows characters from the fast and slow tasks intermixed
 *     within a single line frame.
 *   - WITH_MUTEX = 1: lines are atomic. Every "[fast N]\r\n" or
 *     "[slow N]\r\n" is on the wire end-to-end before any other line
 *     starts.
 *
 * Common mistake: using xSemaphoreCreateBinary instead of
 * xSemaphoreCreateMutex. The take/give API looks identical and the racy
 * vs clean comparison appears to work — but a binary semaphore does NOT
 * implement priority inheritance, so if a higher-priority task ever
 * contends, you have the priority-inversion pathology from Lecture 3 and
 * Challenge 1. Use a mutex.
 *
 * API references cited:
 *   xSemaphoreCreateMutex     — https://www.freertos.org/CreateMutex.html
 *   xSemaphoreTake            — https://www.freertos.org/a00122.html
 *   xSemaphoreGive            — https://www.freertos.org/a00123.html
 *   uart_init / uart_puts     — pico-sdk hardware_uart
 *
 * RP2040 datasheet references:
 *   §4.2 (UART, PL011), pp. 410-431 (UARTDR write semantics, FIFO depth)
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* Set to 0 to demonstrate the race, 1 for the corrected version.
 * Build and capture twice; commit both Saleae captures. */
#ifndef WITH_MUTEX
#define WITH_MUTEX   1
#endif

/* ---- Pin and UART configuration ---------------------------------------- */

#define UART_ID         uart0
#define UART_BAUD       115200u
#define UART_TX_PIN     0u
#define UART_RX_PIN     1u

/* ---- Task tuning ------------------------------------------------------- */

#define PRIO_FAST_TASK  2u
#define PRIO_SLOW_TASK  2u
#define STK_TASK        1024u   /* printf-heavy; size accordingly */

#define FAST_PERIOD_MS  10u     /* 100 Hz */
#define SLOW_PERIOD_MS  100u    /* 10 Hz */

/* ---- Module state ------------------------------------------------------ */

static SemaphoreHandle_t xUartMutex = NULL;

/* ---- The protected print --------------------------------------------- *
 *
 * If WITH_MUTEX is 1, takes the UART mutex around the write.
 * The mutex is created in main() before the scheduler starts.
 *
 * The critical section is the entire uart_puts call — we hold the mutex
 * for the duration of the bytes-to-wire transfer. The PL011 has a 32-byte
 * FIFO so a short string completes quickly; for longer payloads consider
 * holding the mutex only across a memcpy into a per-task line buffer and
 * letting a gatekeeper task drain to UART. That pattern is shown in the
 * mini-project's stretch goal.
 */
static void protected_puts(const char *s)
{
#if WITH_MUTEX
    if (xSemaphoreTake(xUartMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uart_puts(UART_ID, s);
        xSemaphoreGive(xUartMutex);
    }
    /* Timeout = silent drop. A production driver would log or escalate. */
#else
    /* RACY: two tasks may interleave their writes byte-by-byte through the
     * UART FIFO. The receiver sees garbled lines. */
    uart_puts(UART_ID, s);
#endif
}

/* ---- Fast task — prints every 10 ms ---------------------------------- */
static void vFastTask(void *pv)
{
    (void)pv;

    char buf[40];
    uint32_t counter = 0;
    const TickType_t period = pdMS_TO_TICKS(FAST_PERIOD_MS);
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        snprintf(buf, sizeof(buf), "[fast %lu]\r\n", (unsigned long)counter++);
        protected_puts(buf);
        vTaskDelayUntil(&last, period);
    }
}

/* ---- Slow task — prints every 100 ms --------------------------------- */
static void vSlowTask(void *pv)
{
    (void)pv;

    char buf[40];
    uint32_t counter = 0;
    const TickType_t period = pdMS_TO_TICKS(SLOW_PERIOD_MS);
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        snprintf(buf, sizeof(buf), "[slow %lu]\r\n", (unsigned long)counter++);
        protected_puts(buf);
        vTaskDelayUntil(&last, period);
    }
}

/* ---- Hooks ------------------------------------------------------------- */

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
    /* Bring up the UART explicitly (we are not using stdio_init_all's
     * pre-configured stdout because we want full control of the wire). */
    uart_init(UART_ID, UART_BAUD);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);

#if WITH_MUTEX
    /* xSemaphoreCreateMutex returns the handle directly; the mutex starts
     * in the "given" state so the first take succeeds immediately. */
    xUartMutex = xSemaphoreCreateMutex();
    configASSERT(xUartMutex != NULL);
#endif

    BaseType_t rc;
    rc = xTaskCreate(vFastTask, "fast", STK_TASK, NULL, PRIO_FAST_TASK, NULL);
    configASSERT(rc == pdPASS);
    rc = xTaskCreate(vSlowTask, "slow", STK_TASK, NULL, PRIO_SLOW_TASK, NULL);
    configASSERT(rc == pdPASS);

    vTaskStartScheduler();

    for (;;) { /* spin — scheduler failed to start */ }
    return 0;
}
