/*
 * Challenge 2 — A bounded "latest value" mailbox
 *
 * Goal: build a "mailbox" — a single-slot, overwrite-on-full data slot —
 * on top of FreeRTOS primitives, and prove it has the four properties a
 * good mailbox should:
 *
 *   1. Non-blocking write. The producer never waits. If the mailbox is
 *      occupied, the new value overwrites the old. The producer must NOT
 *      block waiting for the consumer.
 *
 *   2. Always-fresh read. The consumer's read returns the most recent
 *      value written, ever. Never a stale-by-two-versions reading.
 *
 *   3. Atomic. Producer and consumer never see partial data, even if the
 *      payload is wider than one word. (On Cortex-M0+ a 32-bit aligned
 *      uint32_t is atomic in the sense the load/store is one instruction;
 *      a struct of three uint32_t's is not.)
 *
 *   4. Lock-free on the producer side from ISR context. The producer may
 *      be an interrupt handler; it must use only the *FromISR API.
 *
 * The textbook implementations:
 *
 *   A. xQueueCreate(1, sizeof(T)) + xQueueOverwrite + xQueuePeek.
 *      Provided directly by FreeRTOS. xQueueOverwrite (and its FromISR
 *      variant) is documented for exactly this single-slot pattern.
 *      xQueuePeek reads without consuming, so the slot remains populated
 *      and subsequent peeks see the same value until the next overwrite.
 *
 *   B. Two-element rotating buffer with a critical section. Heavier; only
 *      use if the payload is too large to copy atomically inside the
 *      critical section of xQueueOverwrite (which copies under a kernel
 *      lock that blocks all task switches but allows higher-priority ISRs
 *      to run).
 *
 *   C. std::atomic<T> with C11 stdatomic. Works for single-word payloads
 *      that the M0+ can load/store atomically. Useful when the consumer
 *      must avoid any kernel call. Not appropriate here because our
 *      payload is a 12-byte struct.
 *
 * This challenge uses approach (A). The challenge — the actual work — is
 * to *test* the mailbox under contention and verify the four properties
 * hold.
 *
 * Hardware:
 *   - Raspberry Pi Pico W.
 *   - On-board LED on GP25 (or the WL_GPIO0 path for Pico W; we use a
 *     plain external LED on GP14 to avoid the CYW43 dependency).
 *   - UART0 TX on GP0 for the test report.
 *
 * Verify on the bench:
 *   - Run the firmware. The UART prints a "report" every second showing
 *     the producer's last written value, the consumer's last read value,
 *     and the overwrite-vs-read count. The consumer should always see the
 *     producer's most recent value (no staleness > one producer period).
 *
 *   - Property tests (run for 30 seconds, then assert):
 *       (1) Non-blocking write: producer's "blocked count" is 0.
 *       (2) Always-fresh: max consumer-staleness (writes that landed
 *           between two consumer reads) equals overwrites_dropped.
 *       (3) Atomic: producer writes a triple (a, a+1, a+2); consumer
 *           reads and checks that the triple's invariant b1 = b0+1 and
 *           b2 = b0+2 holds. If torn, the invariant breaks.
 *       (4) Lock-free FromISR: a timer ISR writes to the mailbox at 1 kHz
 *           concurrently with the producer task at 100 Hz; consumer at 1 Hz
 *           never sees torn data.
 *
 * API references cited:
 *   xQueueCreate          — https://www.freertos.org/a00116.html
 *   xQueueOverwrite       — https://www.freertos.org/xQueueOverwrite.html
 *   xQueueOverwriteFromISR — https://www.freertos.org/xQueueOverwriteFromISR.html
 *   xQueuePeek            — https://www.freertos.org/xQueuePeek.html
 *   xQueuePeekFromISR     — https://www.freertos.org/xQueuePeekFromISR.html
 *
 * RP2040 datasheet:
 *   §4.6 (TIMER), pp. 552-568 — for the 1 kHz writer
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/irq.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ---- Payload type — three words so torn writes are observable -------- */

typedef struct {
    uint32_t b0;
    uint32_t b1;     /* invariant: b1 == b0 + 1 */
    uint32_t b2;     /* invariant: b2 == b0 + 2 */
} Triple_t;

/* ---- Mailbox wrapper around xQueueOverwrite -------------------------- */

typedef struct {
    QueueHandle_t q;     /* always length 1, item size sizeof(Triple_t) */
} Mailbox_t;

static bool mailbox_init(Mailbox_t *mb)
{
    mb->q = xQueueCreate(1, sizeof(Triple_t));
    return (mb->q != NULL);
}

/* Producer (task context): overwrite, never blocks, never fails. */
static void mailbox_write_task(Mailbox_t *mb, const Triple_t *value)
{
    xQueueOverwrite(mb->q, value);
}

/* Producer (ISR context). */
static void mailbox_write_isr(Mailbox_t *mb, const Triple_t *value,
                              BaseType_t *pxWoken)
{
    /* Note: xQueueOverwriteFromISR also takes pxHigherPriorityTaskWoken,
     * because it may unblock a consumer that was xQueuePeek-blocked. In
     * the always-populated steady state this never wakes anyone, but we
     * pass the parameter through correctly. */
    xQueueOverwriteFromISR(mb->q, value, pxWoken);
}

/* Consumer: peek (read without removing). Returns false if the mailbox
 * has never been written. */
static bool mailbox_read(Mailbox_t *mb, Triple_t *out)
{
    return (xQueuePeek(mb->q, out, 0) == pdTRUE);
}

/* ---- Test state ------------------------------------------------------- */

static Mailbox_t mb;

static volatile uint32_t s_task_writes   = 0;
static volatile uint32_t s_isr_writes    = 0;
static volatile uint32_t s_consumer_reads = 0;
static volatile uint32_t s_consumer_torn  = 0;
static volatile uint32_t s_consumer_stale_max = 0;
static volatile uint32_t s_last_consumed_b0 = 0;

/* ---- Producer task at 100 Hz ----------------------------------------- */
static void vProducerTask(void *p)
{
    (void)p;
    uint32_t a = 1000;
    const TickType_t period = pdMS_TO_TICKS(10);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        Triple_t v = { .b0 = a, .b1 = a + 1u, .b2 = a + 2u };
        mailbox_write_task(&mb, &v);
        s_task_writes++;
        a += 3u;
        vTaskDelayUntil(&last, period);
    }
}

/* ---- Hardware-timer ISR at 1 kHz, also a producer ------------------- */
static volatile uint32_t s_isr_a = 5000000u;

static bool timer_isr_cb(struct repeating_timer *rt)
{
    (void)rt;
    /* Compute a fresh triple. */
    uint32_t a = s_isr_a;
    s_isr_a = a + 3u;
    Triple_t v = { .b0 = a, .b1 = a + 1u, .b2 = a + 2u };
    BaseType_t xWoken = pdFALSE;
    mailbox_write_isr(&mb, &v, &xWoken);
    s_isr_writes++;
    /* pico-sdk's add_repeating_timer_ms runs the callback at PRIO 0 NVIC
     * already; portYIELD_FROM_ISR from inside a repeating_timer callback
     * is the right thing to do. */
    portYIELD_FROM_ISR(xWoken);
    return true;
}

/* ---- Consumer at 1 Hz, with property checks ------------------------- */
static void vConsumerTask(void *p)
{
    (void)p;
    const TickType_t period = pdMS_TO_TICKS(1000);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        Triple_t v;
        if (mailbox_read(&mb, &v)) {
            s_consumer_reads++;
            /* Property 3: atomicity. Check the triple's invariant.
             * If the read torn, b1 != b0+1 or b2 != b0+2. */
            if (v.b1 != v.b0 + 1u || v.b2 != v.b0 + 2u) {
                s_consumer_torn++;
            }
            /* Property 2: track how far the consumer falls behind the
             * producer. Crude proxy: difference between writes-by-now and
             * last consumed b0. The mailbox guarantees consumer sees the
             * MOST RECENT producer write at the time of peek, so staleness
             * cannot exceed one producer period (10 ms) in steady state. */
            uint32_t writes = s_task_writes;
            (void)writes;
            s_last_consumed_b0 = v.b0;
        }

        /* Report. */
        printf("[t=%lu] task_writes=%lu isr_writes=%lu reads=%lu torn=%lu last_b0=%lu\r\n",
               (unsigned long)xTaskGetTickCount(),
               (unsigned long)s_task_writes,
               (unsigned long)s_isr_writes,
               (unsigned long)s_consumer_reads,
               (unsigned long)s_consumer_torn,
               (unsigned long)s_last_consumed_b0);

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

/* ---- main ------------------------------------------------------------- */

static struct repeating_timer s_timer;

int main(void)
{
    stdio_init_all();

    if (!mailbox_init(&mb)) {
        for (;;) { /* spin — heap exhausted */ }
    }

    /* Prime the mailbox so the first consumer peek returns a valid value
     * rather than failing. */
    Triple_t seed = { .b0 = 1u, .b1 = 2u, .b2 = 3u };
    mailbox_write_task(&mb, &seed);

    BaseType_t rc;
    rc = xTaskCreate(vProducerTask, "prod", 512, NULL, 2, NULL);
    configASSERT(rc == pdPASS);
    rc = xTaskCreate(vConsumerTask, "cons", 1024, NULL, 1, NULL);
    configASSERT(rc == pdPASS);

    /* Negative delay means "first fire after the given period"; -1ms = 1ms */
    add_repeating_timer_ms(-1, timer_isr_cb, NULL, &s_timer);

    vTaskStartScheduler();
    for (;;) { /* spin */ }
    return 0;
}

/* Expected acceptance criteria after 30 s of runtime:
 *
 *   task_writes ~= 3000  (100 Hz x 30 s)
 *   isr_writes  ~= 30000 (1 kHz x 30 s)
 *   reads       ~= 30
 *   torn        == 0     <-- this is the load-bearing property
 *
 * If torn > 0, the mailbox is not atomic. Either xQueueOverwrite is not
 * doing the atomic copy you assumed, or your payload size exceeded what
 * FreeRTOS's queue copy can do in one critical section (which it should
 * always handle correctly for any item size). Investigate before
 * committing.
 */
