/*
 * Exercise 1 — Blinky as two tasks
 *
 * Goal: build the simplest possible FreeRTOS program. Two LEDs blink at
 * different rates, each driven by its own task. There is no IPC, no shared
 * data, and no contention. The point is to bring up the kernel, prove that
 * the scheduler is alive, and observe two independent timing loops cohabiting
 * one Cortex-M0+ core.
 *
 * Hardware:
 *   - Raspberry Pi Pico W (or Pico — both work; the on-board LED differs).
 *   - Two external LEDs with ~330 ohm series resistors on:
 *       GP14  (LED A, blinks at 2 Hz)
 *       GP15  (LED B, blinks at 5 Hz)
 *   - GND common with the Pico.
 *   - Optional: a logic analyzer or scope on GP14 and GP15 to verify the
 *     timing. The 2 Hz waveform should have 250 ms high and 250 ms low; the
 *     5 Hz should be 100 ms / 100 ms.
 *
 * Build:
 *   See mini-project/README.md for the CMakeLists.txt pattern that wires
 *   pico-sdk and FreeRTOS-Kernel together. This file's expected build
 *   artefact is the .elf for a project named `ex01_two_blinks`.
 *
 * Verify on the bench:
 *   - Both LEDs blink independently after flashing.
 *   - The 2 Hz LED has a 250 ms duty (use the logic-analyzer measurement
 *     tools to confirm — should be within +/- 1 ms).
 *   - The 5 Hz LED has a 100 ms duty.
 *   - Neither LED stalls when the other is mid-transition — independent
 *     scheduling, not a single superloop.
 *
 * API references cited:
 *   xTaskCreate                — https://www.freertos.org/a00125.html
 *   vTaskDelayUntil            — https://www.freertos.org/vtaskdelayuntil.html
 *   xTaskGetTickCount          — https://www.freertos.org/a00021.html#GetTickCount
 *   vTaskStartScheduler        — https://www.freertos.org/a00132.html
 *
 * RP2040 datasheet references:
 *   §2.4 (Cortex-M0+),          pp. 65-73
 *   §2.4.1 (SysTick),            pp. 96-98
 *   §2.19 (GPIO and pads),       pp. 235-300
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"

/* ---- Pin assignments ---------------------------------------------------- */

#define PIN_LED_A    14u    /* 2 Hz blinker */
#define PIN_LED_B    15u    /* 5 Hz blinker */

/* ---- Task priorities ---------------------------------------------------- */

#define PRIO_LED_A   2u
#define PRIO_LED_B   2u     /* same priority — kernel round-robins on each tick */

/* ---- Stack depths (in words; 4 bytes per word on Cortex-M0+) ----------- */

#define STK_LED_A    256u
#define STK_LED_B    256u

/* ---- Task: 2 Hz blinker on GP14 ---------------------------------------- *
 *
 * Uses vTaskDelayUntil for drift-free periodic timing.
 * xLastWakeTime is initialized once before the loop and updated by the API
 * on every iteration.
 */
static void vBlinkATask(void *pvParameters)
{
    (void)pvParameters;

    const TickType_t xPeriod = pdMS_TO_TICKS(250);   /* half-period of 2 Hz */
    TickType_t xLastWakeTime = xTaskGetTickCount();

    gpio_init(PIN_LED_A);
    gpio_set_dir(PIN_LED_A, GPIO_OUT);
    gpio_put(PIN_LED_A, 0);

    bool level = false;
    for (;;) {
        level = !level;
        gpio_put(PIN_LED_A, level);
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/* ---- Task: 5 Hz blinker on GP15 ---------------------------------------- */
static void vBlinkBTask(void *pvParameters)
{
    (void)pvParameters;

    const TickType_t xPeriod = pdMS_TO_TICKS(100);   /* half-period of 5 Hz */
    TickType_t xLastWakeTime = xTaskGetTickCount();

    gpio_init(PIN_LED_B);
    gpio_set_dir(PIN_LED_B, GPIO_OUT);
    gpio_put(PIN_LED_B, 0);

    bool level = false;
    for (;;) {
        level = !level;
        gpio_put(PIN_LED_B, level);
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/* ---- Stack overflow hook ----------------------------------------------- *
 *
 * Enabled by configCHECK_FOR_STACK_OVERFLOW = 2 in FreeRTOSConfig.h.
 * If either task ever pushes past its allocated stack, the kernel calls us
 * with the offending task's name. We spin so the failure is observable on
 * the bench (the LEDs stop) and a debugger can attach.
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    /* If you have a debugger attached you can read pcTaskName off the stack. */
    taskDISABLE_INTERRUPTS();
    for (;;) {
        /* spin */
    }
}

/* ---- Malloc-failed hook ------------------------------------------------ *
 *
 * configUSE_MALLOC_FAILED_HOOK = 1 in FreeRTOSConfig.h. If pvPortMalloc
 * (called from xTaskCreate, xQueueCreate, etc.) returns NULL we land here
 * before the higher-level API returns pdFAIL.
 */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {
        /* spin */
    }
}

/* ---- main -------------------------------------------------------------- */
int main(void)
{
    /* Standard pico-sdk bring-up. We do not use stdio in this exercise but
     * we initialize it anyway so the USB-CDC serial is available for adding
     * printf debugging during development. */
    stdio_init_all();

    BaseType_t rc;

    rc = xTaskCreate(
        vBlinkATask,        /* function */
        "blinkA",           /* name */
        STK_LED_A,          /* stack depth (words) */
        NULL,               /* parameters */
        PRIO_LED_A,         /* priority */
        NULL                /* handle out (we don't need it) */
    );
    configASSERT(rc == pdPASS);

    rc = xTaskCreate(
        vBlinkBTask,
        "blinkB",
        STK_LED_B,
        NULL,
        PRIO_LED_B,
        NULL
    );
    configASSERT(rc == pdPASS);

    /* Hand control to the scheduler. This call does not return. */
    vTaskStartScheduler();

    /* If we get here, the kernel could not allocate its idle-task or
     * timer-service-task. Spin to make the failure observable. */
    for (;;) {
        /* spin */
    }
    return 0;
}
