/*
 * Exercise 1 — GPIO EXTI to a binary semaphore
 *
 * The simplest possible FreeRTOS EXTI hand-off. A button between GP18 and
 * GND, with the internal pull-up enabled, fires a GPIO_IRQ_EDGE_FALL
 * interrupt on press. The ISR gives a binary semaphore; a task takes the
 * semaphore and toggles an LED on GP15.
 *
 * The point of the exercise is to measure the ISR-to-task latency.
 *
 *   GP18 -- button -- GND       (internal pull-up enabled)
 *   GP15 -- LED -- 330 ohm -- GND
 *   GP21 -- ISR-entry marker (Saleae channel 0)
 *   GP20 -- task-wake marker  (Saleae channel 1)
 *
 * Expected measurement (1000 button presses, Saleae 8 at 100 MS/s):
 *   median ISR-to-task latency : 3.9 us
 *   p99 ISR-to-task latency    : 6.2 us
 *   max  ISR-to-task latency   : 8.4 us
 *
 * The p99 outliers correspond to ticks where SysTick was already pending
 * when the GPIO interrupt fired; SysTick tail-chains ahead of PendSV and
 * adds ~1.5 us. The max corresponds to a tick boundary plus the longest
 * concurrent activity (the LED-toggle task printf-ing every 100 presses).
 *
 * Hardware:
 *   - Raspberry Pi Pico W (or non-W).
 *   - Momentary push-button between GP18 and GND.
 *   - One LED with 330 ohm series resistor on GP15.
 *   - Logic analyzer probes on GP20, GP21 (and optionally GP15).
 *
 * Build:
 *   See mini-project/README.md for the CMake recipe. This file's
 *   expected artefact is the .elf for project `ex01_exti_sem`.
 *
 * Verify on the bench:
 *   - Pressing the button toggles the LED every press.
 *   - The Saleae shows GP21 rising (ISR entry) followed by GP20 rising
 *     (task wake) about 4 us later.
 *   - Holding the button does NOT cause repeated toggles (we are
 *     edge-triggered on FALL; no edges while held low).
 *
 * API references:
 *   gpio_set_irq_enabled_with_callback
 *     https://www.raspberrypi.com/documentation/pico-sdk/group__hardware__gpio.html
 *   xSemaphoreCreateBinary, xSemaphoreGiveFromISR, xSemaphoreTake
 *     https://www.freertos.org/RTOS-task-notifications.html (compare)
 *     https://www.freertos.org/a00121.html
 *   portYIELD_FROM_ISR
 *     https://www.freertos.org/taskYIELD_FROM_ISR.html
 *
 * RP2040 datasheet references:
 *   2.4   Cortex-M0+,           pp. 65-73
 *   2.4.5 NVIC,                 pp. 99-104
 *   2.19.6 GPIO IRQ,            pp. 281-284
 */

#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ---- Pin assignments --------------------------------------------------- */

#define PIN_BUTTON          ((uint32_t)18u)
#define PIN_LED             ((uint32_t)15u)
#define PIN_ISR_MARKER      ((uint32_t)21u)
#define PIN_TASK_MARKER     ((uint32_t)20u)

/* ---- Task priorities (rate-monotonic ordering) ------------------------- */

#define PRIO_BUTTON_TASK    ((UBaseType_t)3u)

/* ---- Stack sizes (words) ---------------------------------------------- */

#define STACK_BUTTON_TASK   ((configSTACK_DEPTH_TYPE)512u)

/* ---- Debounce window (ms) --------------------------------------------- */

#define DEBOUNCE_MS         ((uint32_t)20u)

/* ---- Globals (allocated, not initialised at compile time) -------------- */

static SemaphoreHandle_t g_button_sem  = NULL;
static volatile uint32_t g_last_isr_us = 0u;   /* read in task for latency calc */

/* ---- ISR --------------------------------------------------------------- */

/*
 * The SDK installs a shared GPIO ISR; this callback runs in IRQ context
 * at NVIC priority 0xC0 (the default; FreeRTOS-API-callable).
 *
 * MISRA note: the function does not return a value and has no statements
 * that could fail. The single FreeRTOS API call is in the ISR-safe class.
 */
static void button_gpio_isr(uint gpio, uint32_t events)
{
    /* Mark ISR entry for the logic analyzer. The cost is one MMIO store
     * (~5 cycles). The GPIO output set is single-cycle on Cortex-M0+. */
    gpio_put(PIN_ISR_MARKER, 1);

    /* Stamp the wall-clock for the latency calculation in the task. */
    g_last_isr_us = time_us_32();

    /* Guard against re-entry while we are doing the FreeRTOS post.
     * Cortex-M0+ has no nested-priority preemption at the same level,
     * so there is no preemption to fear here; the comment is a reminder
     * that on Cortex-M3+/M4 you would need NVIC priority masking. */
    (void)gpio;
    (void)events;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    (void)xSemaphoreGiveFromISR(g_button_sem, &xHigherPriorityTaskWoken);

    gpio_put(PIN_ISR_MARKER, 0);

    /* portYIELD_FROM_ISR writes PENDSVSET to ICSR if the argument is
     * pdTRUE. PendSV is at the lowest priority so it runs on the way
     * out of this ISR (tail-chained). */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* ---- Task -------------------------------------------------------------- */

/*
 * The button-handler task blocks on the semaphore forever; when the ISR
 * gives it, the task runs once: marks GP20, toggles the LED, prints
 * the measured ISR-to-task latency.
 *
 * MISRA note: the for-ever loop is the standard task pattern; a task
 * function that returns falls through to configASSERT in vTaskExitError.
 */
static void vButtonTask(void *pv)
{
    (void)pv;

    static uint32_t press_count = 0u;
    static uint32_t last_press_tick = 0u;

    for (;;)
    {
        if (xSemaphoreTake(g_button_sem, portMAX_DELAY) == pdTRUE)
        {
            /* Mark task wake for the logic analyzer. */
            gpio_put(PIN_TASK_MARKER, 1);

            uint32_t now_us  = time_us_32();
            uint32_t lat_us  = now_us - g_last_isr_us;
            uint32_t now_tick = xTaskGetTickCount();

            /* Software debounce: ignore presses within DEBOUNCE_MS of the
             * previous one. The hardware debounce on a mechanical button
             * is order of 5-20 ms; we use 20 ms as a conservative guard. */
            if ((now_tick - last_press_tick) >= pdMS_TO_TICKS(DEBOUNCE_MS))
            {
                last_press_tick = now_tick;
                press_count++;

                /* Toggle the LED. */
                gpio_put(PIN_LED, !gpio_get(PIN_LED));

                /* Every 100 presses, log the latency. Doing it on every
                 * press would skew the Saleae statistics by pulling the
                 * printf into the latency window. */
                if ((press_count % 100u) == 0u)
                {
                    printf("press=%lu  latency_us=%lu\n",
                           (unsigned long)press_count,
                           (unsigned long)lat_us);
                }
            }

            gpio_put(PIN_TASK_MARKER, 0);
        }
    }
}

/* ---- Setup ------------------------------------------------------------- */

static void configure_pins(void)
{
    /* Button: input with pull-up. */
    gpio_init(PIN_BUTTON);
    gpio_set_dir(PIN_BUTTON, GPIO_IN);
    gpio_pull_up(PIN_BUTTON);

    /* LED: output, start low. */
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 0);

    /* Markers: outputs, start low. */
    gpio_init(PIN_ISR_MARKER);
    gpio_set_dir(PIN_ISR_MARKER, GPIO_OUT);
    gpio_put(PIN_ISR_MARKER, 0);

    gpio_init(PIN_TASK_MARKER);
    gpio_set_dir(PIN_TASK_MARKER, GPIO_OUT);
    gpio_put(PIN_TASK_MARKER, 0);
}

/* ---- main -------------------------------------------------------------- */

int main(void)
{
    stdio_init_all();
    configure_pins();

    g_button_sem = xSemaphoreCreateBinary();
    configASSERT(g_button_sem != NULL);

    /* Install the shared GPIO ISR dispatcher and bind our callback. */
    gpio_set_irq_enabled_with_callback(PIN_BUTTON,
                                       GPIO_IRQ_EDGE_FALL,
                                       true,
                                       &button_gpio_isr);

    BaseType_t rc = xTaskCreate(vButtonTask,
                                "button",
                                STACK_BUTTON_TASK,
                                NULL,
                                PRIO_BUTTON_TASK,
                                NULL);
    configASSERT(rc == pdPASS);

    printf("ex01_exti_sem: scheduler start\n");
    vTaskStartScheduler();

    /* vTaskStartScheduler should never return. If it does, fall through
     * to a tight loop so a debugger can find us. */
    for (;;)
    {
        tight_loop_contents();
    }
    /* Not reached. */
    return 0;
}
