/*
 * Challenge 2 — ISR latency jitter hunt
 *
 * Exercise 1 measured median ~4 us and p99 ~6 us for the button
 * ISR-to-task hand-off. This challenge stress-tests the same hand-off
 * under three concurrent loads and asks you to identify the cause of
 * each tail-latency outlier.
 *
 * The setup:
 *   - One periodic timer (the SysTick) firing at 1 kHz.
 *   - One periodic load task (priority 1) doing 200 us of busy work
 *     every 500 us (40 % duty CPU-bound).
 *   - One I2C-burst task (priority 2) doing a 250 us blocking I2C
 *     transaction every 5 ms.
 *   - The button-ISR / consumer pair at priority 3.
 *
 * Run the system for 10 minutes. Capture the GP20 (task-wake marker)
 * minus GP21 (ISR-entry marker) deltas for every button press. Sort
 * the deltas. The distribution has a clearly bimodal tail at ~10 us
 * and ~280 us. Your task is to identify which load source causes
 * which tail.
 *
 * Expected answers (revealed in your write-up, not given here):
 *   - The ~10 us bin: SysTick handler ran in between ISR and PendSV.
 *     SysTick is at priority 0xC0 (same as the GPIO ISR), tail-chains
 *     ahead of PendSV. Cost: ~1.5 us SysTick body + ~0.5 us
 *     dispatch + ~0.5 us extra context switch = ~2.5 us added to the
 *     ~4 us baseline.
 *   - The ~280 us bin: I2C-burst task held the kernel critical section
 *     across the entire transaction. SDK i2c_write_blocking takes
 *     ~280 us at 100 kHz for a 9-byte payload. Your button ISR fired
 *     while the kernel critical section was masked (priority <= 0xC0
 *     all masked), so the ISR is deferred by the remaining I2C time.
 *
 * The instructive lesson: kernel critical sections are the dominant
 * source of ISR-latency jitter in well-tuned firmware. Reducing
 * critical-section length anywhere in the system reduces jitter
 * everywhere. This is why the gatekeeper pattern (no mutex around
 * the resource) is often preferred over the mutex pattern in latency-
 * critical builds.
 *
 * The challenge deliverable:
 *   - Saleae capture of >= 1000 button presses with all loads on.
 *   - JITTER-ANALYSIS.md identifying both tail bins by source, with
 *     numerical estimates of the contribution.
 *   - A second capture with the I2C task disabled, showing the ~280 us
 *     tail removed but the ~10 us tail intact.
 *   - A third capture with the SysTick disabled (configUSE_PREEMPTION
 *     = 0 cooperative mode), showing both tails removed.
 *
 * Hardware:
 *   GP18 -- button (as in Exercise 1)
 *   GP15 -- LED
 *   GP20 -- task-wake marker
 *   GP21 -- ISR-entry marker
 *   GP19 -- "I2C task is in critical section" marker
 *   GP14 -- "load task is busy" marker
 *
 *   I2C0 SDA -> GP4, I2C0 SCL -> GP5, addressed to a non-existent
 *   device 0x77 - the transaction times out at the full 250 us. (We
 *   use the BMP280 from Week 6 if you have one; otherwise the no-ACK
 *   path produces the same timing.)
 */

#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/timer.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ---- Pin assignments --------------------------------------------------- */

#define PIN_BUTTON          ((uint32_t)18u)
#define PIN_LED             ((uint32_t)15u)
#define PIN_ISR_MARKER      ((uint32_t)21u)
#define PIN_TASK_MARKER     ((uint32_t)20u)
#define PIN_I2C_BUSY        ((uint32_t)19u)
#define PIN_LOAD_BUSY       ((uint32_t)14u)

#define PIN_I2C_SDA         ((uint32_t)4u)
#define PIN_I2C_SCL         ((uint32_t)5u)
#define I2C_INSTANCE        i2c0
#define I2C_TARGET          ((uint8_t)0x77u)   /* BMP280 or no-ACK */

/* ---- Priorities -------------------------------------------------------- */

#define PRIO_BUTTON         ((UBaseType_t)3u)
#define PRIO_I2C            ((UBaseType_t)2u)
#define PRIO_LOAD           ((UBaseType_t)1u)

#define STACK_DEFAULT       ((configSTACK_DEPTH_TYPE)768u)

/* ---- Globals ---------------------------------------------------------- */

static SemaphoreHandle_t  g_button_sem    = NULL;
static volatile uint32_t  g_last_isr_us   = 0u;
static volatile uint32_t  g_max_latency   = 0u;
static volatile uint32_t  g_press_count   = 0u;

/* ---- ISR --------------------------------------------------------------- */

static void button_gpio_isr(uint gpio, uint32_t events)
{
    (void)gpio;
    (void)events;

    gpio_put(PIN_ISR_MARKER, 1);
    g_last_isr_us = time_us_32();

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    (void)xSemaphoreGiveFromISR(g_button_sem, &xHigherPriorityTaskWoken);

    gpio_put(PIN_ISR_MARKER, 0);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* ---- Button task ------------------------------------------------------ */

static void vButtonTask(void *pv)
{
    (void)pv;

    for (;;)
    {
        if (xSemaphoreTake(g_button_sem, portMAX_DELAY) == pdTRUE)
        {
            gpio_put(PIN_TASK_MARKER, 1);

            const uint32_t now = time_us_32();
            const uint32_t lat = now - g_last_isr_us;
            if (lat > g_max_latency)
            {
                g_max_latency = lat;
            }
            g_press_count++;

            gpio_put(PIN_LED, !gpio_get(PIN_LED));

            if ((g_press_count % 100u) == 0u)
            {
                printf("press=%lu  lat_us=%lu  peak_us=%lu\n",
                       (unsigned long)g_press_count,
                       (unsigned long)lat,
                       (unsigned long)g_max_latency);
            }

            gpio_put(PIN_TASK_MARKER, 0);
        }
    }
}

/* ---- I2C task --------------------------------------------------------- */

static void vI2cTask(void *pv)
{
    (void)pv;

    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(5u);
    uint8_t dummy_reg = (uint8_t)0xD0u;   /* WHO_AM_I on BMP280 */
    uint8_t dummy_resp[1];

    for (;;)
    {
        gpio_put(PIN_I2C_BUSY, 1);
        (void)i2c_write_blocking(I2C_INSTANCE, I2C_TARGET, &dummy_reg,
                                 (size_t)1, true);
        (void)i2c_read_blocking(I2C_INSTANCE, I2C_TARGET, dummy_resp,
                                (size_t)1, false);
        gpio_put(PIN_I2C_BUSY, 0);
        vTaskDelayUntil(&last, period);
    }
}

/* ---- Load task -------------------------------------------------------- */

static void vLoadTask(void *pv)
{
    (void)pv;

    for (;;)
    {
        const uint32_t start = time_us_32();
        gpio_put(PIN_LOAD_BUSY, 1);
        while ((time_us_32() - start) < (uint32_t)200u)
        {
            __asm volatile ("nop");
        }
        gpio_put(PIN_LOAD_BUSY, 0);
        /* 300 us idle gap = 40 % duty cycle on the load. */
        vTaskDelay(pdMS_TO_TICKS(1u));
    }
}

/* ---- Setup ------------------------------------------------------------- */

static void configure_pins(void)
{
    gpio_init(PIN_BUTTON);
    gpio_set_dir(PIN_BUTTON, GPIO_IN);
    gpio_pull_up(PIN_BUTTON);

    const uint32_t outs[] = { PIN_LED, PIN_ISR_MARKER, PIN_TASK_MARKER,
                              PIN_I2C_BUSY, PIN_LOAD_BUSY };
    for (uint32_t i = 0u; i < (uint32_t)5u; ++i)
    {
        gpio_init(outs[i]);
        gpio_set_dir(outs[i], GPIO_OUT);
        gpio_put(outs[i], 0);
    }

    i2c_init(I2C_INSTANCE, (uint32_t)100000u);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);
}

/* ---- main -------------------------------------------------------------- */

int main(void)
{
    stdio_init_all();
    configure_pins();

    g_button_sem = xSemaphoreCreateBinary();
    configASSERT(g_button_sem != NULL);

    gpio_set_irq_enabled_with_callback(PIN_BUTTON,
                                       GPIO_IRQ_EDGE_FALL,
                                       true,
                                       &button_gpio_isr);

    BaseType_t rc;
    rc = xTaskCreate(vButtonTask, "button", STACK_DEFAULT, NULL,
                     PRIO_BUTTON, NULL);
    configASSERT(rc == pdPASS);
    rc = xTaskCreate(vI2cTask,    "i2c",    STACK_DEFAULT, NULL,
                     PRIO_I2C,    NULL);
    configASSERT(rc == pdPASS);
    rc = xTaskCreate(vLoadTask,   "load",   STACK_DEFAULT, NULL,
                     PRIO_LOAD,   NULL);
    configASSERT(rc == pdPASS);

    printf("ch02_jitter_hunt: scheduler start\n");
    vTaskStartScheduler();

    for (;;)
    {
        tight_loop_contents();
    }
    return 0;
}
