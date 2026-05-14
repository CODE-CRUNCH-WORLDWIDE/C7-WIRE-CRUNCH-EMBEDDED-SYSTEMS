/*
 * Exercise 2 — Button ISR posts to a queue, task consumes
 *
 * Goal: introduce ISR-to-task hand-off through a queue. The button GPIO
 * falling-edge interrupt fires on the press. The handler runs in ISR
 * context, captures the tick count, and posts a ButtonEvent_t to a queue.
 * A task blocks on the queue and toggles the LED state on each consumed
 * event.
 *
 * The pattern this exercise demonstrates:
 *
 *   ISR (short, non-blocking) --xQueueSendFromISR--> queue --xQueueReceive--> task (full task-context API allowed)
 *
 * is the foundation of every interrupt-driven RTOS program. Never do
 * non-trivial work in the ISR; defer it to a task.
 *
 * Hardware:
 *   - Raspberry Pi Pico W.
 *   - Tactile push-button between GP15 and GND. Internal pull-up enabled.
 *     A press grounds GP15 -> falling edge on the input.
 *   - LED with ~330 ohm series resistor on GP14 to GND.
 *   - Optional: logic analyzer on GP15 (the button line) and GP14 (the LED)
 *     to verify that every falling edge on GP15 produces exactly one LED
 *     state change on GP14, within a few microseconds of latency.
 *
 * Verify on the bench:
 *   - LED toggles on each button press.
 *   - On a logic analyzer, the latency from the GP15 falling edge to the
 *     GP14 transition is < 100 us under no load. Bounded by the ISR latency
 *     (~12 cycles on M0+ ~ 100 ns at 125 MHz) plus the FreeRTOS PendSV
 *     context switch (~84 cycles ~ 700 ns) plus the GPIO write.
 *   - If you press faster than the task can drain the queue, the queue
 *     fills and xQueueSendFromISR returns errQUEUE_FULL. The queue length
 *     is 8 in this exercise so under typical key-bounce conditions you will
 *     not see overflow. To exercise overflow deliberately, reduce QLEN to
 *     1 and tap the button rapidly.
 *
 * Debouncing note: real tactile switches bounce on the order of 5-10 ms.
 * We do a software debounce in the ISR by tracking the last accepted
 * tick-count and rejecting events that arrive within 25 ms of the previous.
 * For a production button driver, prefer a timer-based debounce in a
 * dedicated task; the ISR approach used here is acceptable for an
 * exercise.
 *
 * API references cited:
 *   xQueueCreate                    — https://www.freertos.org/a00116.html
 *   xQueueSendFromISR               — https://www.freertos.org/a00119.html
 *   xQueueReceive                   — https://www.freertos.org/a00118.html
 *   portYIELD_FROM_ISR              — https://www.freertos.org/taskYIELD_FROM_ISR.html
 *   xTaskGetTickCountFromISR        — https://www.freertos.org/a00021.html
 *   gpio_set_irq_enabled_with_callback — pico-sdk hardware_gpio
 *
 * RP2040 datasheet references:
 *   §2.4.5 (NVIC),    pp. 99-104
 *   §2.19   (GPIO IO_BANK0 interrupts), pp. 248-256
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ---- Pin assignments ---------------------------------------------------- */

#define PIN_BUTTON   15u
#define PIN_LED      14u

/* ---- Tunables ---------------------------------------------------------- */

#define QLEN              8u
#define DEBOUNCE_TICKS    pdMS_TO_TICKS(25)

#define PRIO_LED_TASK     2u
#define STK_LED_TASK      512u

/* ---- Event type carried on the queue ----------------------------------- */

typedef struct {
    TickType_t tick;        /* tick count at which the ISR fired */
    uint32_t   edge_id;     /* monotonic counter for tracking drops */
} ButtonEvent_t;

/* ---- Module state ------------------------------------------------------ */

static QueueHandle_t xButtonQueue = NULL;
static volatile TickType_t s_last_accepted_tick = 0;
static volatile uint32_t   s_edge_counter       = 0;
static volatile uint32_t   s_dropped_due_to_full = 0;
static volatile uint32_t   s_dropped_due_to_bounce = 0;

/* ---- ISR --------------------------------------------------------------- *
 *
 * The pico-sdk routes every IO_BANK0 GPIO interrupt through a single
 * callback you register with gpio_set_irq_callback. The callback receives
 * the pin number and the event mask. We only handle PIN_BUTTON in this
 * exercise.
 *
 * The ISR must:
 *   1. Be short — defer all non-trivial work to a task.
 *   2. Use only the *FromISR API. No vTaskDelay, no xQueueSend (without
 *      FromISR).
 *   3. Initialize xHigherPriorityTaskWoken to pdFALSE, pass it to every
 *      *FromISR call that can wake a task, and on the way out call
 *      portYIELD_FROM_ISR(xHigherPriorityTaskWoken).
 */
static void gpio_isr_handler(uint gpio, uint32_t events)
{
    if (gpio != PIN_BUTTON) {
        return;
    }
    /* We only registered the falling-edge handler so this should always be
     * GPIO_IRQ_EDGE_FALL, but check defensively. */
    if ((events & GPIO_IRQ_EDGE_FALL) == 0u) {
        return;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    TickType_t now = xTaskGetTickCountFromISR();

    /* Software debounce: reject edges within DEBOUNCE_TICKS of the last
     * accepted one. */
    if ((TickType_t)(now - s_last_accepted_tick) < DEBOUNCE_TICKS) {
        s_dropped_due_to_bounce++;
        return;
    }
    s_last_accepted_tick = now;

    ButtonEvent_t evt = {
        .tick    = now,
        .edge_id = ++s_edge_counter,
    };

    if (xQueueSendFromISR(xButtonQueue, &evt, &xHigherPriorityTaskWoken) != pdTRUE) {
        /* queue was full — the LED task is slower than the press rate.
         * Count and drop. A production driver would log or escalate. */
        s_dropped_due_to_full++;
    }

    /* Request a yield if the queue wake unblocked a higher-priority task. */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* ---- LED task — consumes from the queue, toggles the LED --------------- */
static void vLedTask(void *pvParameters)
{
    (void)pvParameters;

    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 0);
    bool level = false;

    ButtonEvent_t evt;
    for (;;) {
        /* Block forever until a button event arrives. The task uses zero CPU
         * while blocked. */
        if (xQueueReceive(xButtonQueue, &evt, portMAX_DELAY) == pdTRUE) {
            level = !level;
            gpio_put(PIN_LED, level);

            /* Optional: print the event for debugging. uart_puts is OK from
             * task context. Comment out in production. */
            printf("button edge_id=%lu tick=%lu drops_full=%lu drops_bounce=%lu\r\n",
                   (unsigned long)evt.edge_id,
                   (unsigned long)evt.tick,
                   (unsigned long)s_dropped_due_to_full,
                   (unsigned long)s_dropped_due_to_bounce);
        }
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
    stdio_init_all();

    /* Create the queue first; it must exist before the ISR can post to it. */
    xButtonQueue = xQueueCreate(QLEN, sizeof(ButtonEvent_t));
    configASSERT(xButtonQueue != NULL);

    /* Configure the button: input with pull-up. */
    gpio_init(PIN_BUTTON);
    gpio_set_dir(PIN_BUTTON, GPIO_IN);
    gpio_pull_up(PIN_BUTTON);

    /* Register the unified IO_BANK0 callback. This call also enables the
     * NVIC line for IO_BANK0; subsequent gpio_set_irq_enabled() calls add
     * pins to the callback. */
    gpio_set_irq_enabled_with_callback(
        PIN_BUTTON,
        GPIO_IRQ_EDGE_FALL,
        true,
        &gpio_isr_handler
    );

    /* Create the LED task. */
    BaseType_t rc = xTaskCreate(
        vLedTask, "led", STK_LED_TASK, NULL, PRIO_LED_TASK, NULL
    );
    configASSERT(rc == pdPASS);

    /* Hand control to the scheduler. */
    vTaskStartScheduler();

    for (;;) { /* spin — scheduler failed to start */ }
    return 0;
}
