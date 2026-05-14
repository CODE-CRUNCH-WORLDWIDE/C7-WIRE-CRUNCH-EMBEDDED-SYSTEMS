/*
 * main.c — Week 7 mini-project, IMU EXTI Pipeline
 *
 * Four-task firmware on the Raspberry Pi Pico W:
 *
 *   vIngest      prio 4   200 Hz   stream-buffer drain, decode, push to estimator
 *   vEstimator   prio 3   100 Hz   Mahony attitude fusion
 *   vDownsample  prio 2    10 Hz   10-sample mean, format line
 *   vGatekeeper  prio 1   event    drain uart_queue, write to UART0
 *
 *   idle         prio 0            __WFI in vApplicationIdleHook
 *
 * No mutexes anywhere. The gatekeeper pattern owns the UART; the two
 * length-1 overwrite queues handle latest-value handoffs without
 * contention.
 *
 * Markers for the Saleae (configurable via the PIN_* macros below):
 *
 *   GP21   ISR-entry pulse                       (latency channel)
 *   GP20   vIngest-wake pulse                    (latency channel)
 *   GP19   vEstimator-wake pulse                 (optional)
 *   GP18   vDownsample-wake pulse                (optional)
 *   GP17   vGatekeeper byte-out (UART TX echo)   (optional)
 *
 * This file is a STARTER. You must implement:
 *   - mpu6050_driver.c (declarations in mpu6050_driver.h).
 *   - estimator.c     (Mahony filter; one function:
 *                       quaternion_t mahony_step(ImuSample_t,
 *                                                quaternion_t prev)).
 *
 * Compile: see CMakeLists.txt in this directory. The build is verified
 * against pico-sdk 1.5.1 and FreeRTOS-Kernel V11.1.0.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/timer.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stream_buffer.h"

#include "mpu6050_driver.h"

/* ---- Pin assignments --------------------------------------------------- */

#define PIN_UART_TX             ((uint32_t)0u)
#define PIN_UART_RX             ((uint32_t)1u)
#define PIN_ISR_MARKER          ((uint32_t)21u)
#define PIN_INGEST_MARKER       ((uint32_t)20u)
#define PIN_EST_MARKER          ((uint32_t)19u)
#define PIN_DS_MARKER           ((uint32_t)18u)

/* ---- UART parameters --------------------------------------------------- */

#define UART_INSTANCE           uart0
#define UART_BAUD_HZ            ((uint32_t)115200u)

/* ---- Priorities (rate-monotonic) -------------------------------------- */

#define PRIO_INGEST             ((UBaseType_t)4u)
#define PRIO_ESTIMATOR          ((UBaseType_t)3u)
#define PRIO_DOWNSAMPLE         ((UBaseType_t)2u)
#define PRIO_GATEKEEPER         ((UBaseType_t)1u)

/* ---- Stacks (words) --------------------------------------------------- */

#define STACK_INGEST            ((configSTACK_DEPTH_TYPE)768u)
#define STACK_ESTIMATOR         ((configSTACK_DEPTH_TYPE)1024u)
#define STACK_DOWNSAMPLE        ((configSTACK_DEPTH_TYPE)512u)
#define STACK_GATEKEEPER        ((configSTACK_DEPTH_TYPE)768u)

/* ---- Queue / stream-buffer sizes -------------------------------------- */

#define MPU_STREAM_BYTES        ((size_t)256u)
#define MPU_STREAM_TRIGGER      ((size_t)MPU6050_FRAME_RAW_LEN)
#define UART_QUEUE_LEN          ((UBaseType_t)8u)

/* ---- Data types ------------------------------------------------------- */

typedef struct
{
    int16_t  accel_x, accel_y, accel_z;
    int16_t  temperature;
    int16_t  gyro_x, gyro_y, gyro_z;
    uint32_t timestamp_us;
    uint32_t isr_to_ingest_us;
} ImuSample_t;

typedef struct
{
    float q0, q1, q2, q3;
} Quaternion_t;

typedef struct
{
    char line[96];
} UartLine_t;

/* ---- Globals ---------------------------------------------------------- */

static StreamBufferHandle_t g_mpu_stream    = NULL;
static QueueHandle_t        g_imu_latest_q  = NULL;
static QueueHandle_t        g_quat_latest_q = NULL;
static QueueHandle_t        g_uart_queue    = NULL;

/* ---- Forward decls of estimator (you implement in estimator.c) -------- */

extern Quaternion_t mahony_step(const ImuSample_t *sample,
                                Quaternion_t prev);

/* ---- vIngest: 200 Hz, drains stream buffer ---------------------------- */

static void vIngest(void *pv)
{
    (void)pv;

    uint8_t  raw[MPU6050_FRAME_RAW_LEN];
    uint32_t isr_ts_us;

    for (;;)
    {
        size_t got = xStreamBufferReceive(g_mpu_stream, raw,
                                          MPU6050_FRAME_RAW_LEN,
                                          portMAX_DELAY);
        if (got != MPU6050_FRAME_RAW_LEN)
        {
            continue;
        }

        gpio_put(PIN_INGEST_MARKER, 1);

        /* Pull the timestamp the ISR appended. Short timeout because
         * the bytes are sent within microseconds of the raw frame. */
        (void)xStreamBufferReceive(g_mpu_stream,
                                   (uint8_t *)&isr_ts_us,
                                   sizeof(isr_ts_us),
                                   pdMS_TO_TICKS(1));

        ImuSample_t s;
        s.accel_x      = (int16_t)((uint16_t)raw[0]  << 8 | raw[1]);
        s.accel_y      = (int16_t)((uint16_t)raw[2]  << 8 | raw[3]);
        s.accel_z      = (int16_t)((uint16_t)raw[4]  << 8 | raw[5]);
        s.temperature  = (int16_t)((uint16_t)raw[6]  << 8 | raw[7]);
        s.gyro_x       = (int16_t)((uint16_t)raw[8]  << 8 | raw[9]);
        s.gyro_y       = (int16_t)((uint16_t)raw[10] << 8 | raw[11]);
        s.gyro_z       = (int16_t)((uint16_t)raw[12] << 8 | raw[13]);
        s.timestamp_us = isr_ts_us;
        s.isr_to_ingest_us = time_us_32() - isr_ts_us;

        (void)xQueueOverwrite(g_imu_latest_q, &s);

        gpio_put(PIN_INGEST_MARKER, 0);
    }
}

/* ---- vEstimator: 100 Hz, Mahony filter -------------------------------- */

static void vEstimator(void *pv)
{
    (void)pv;

    Quaternion_t q = { 1.0f, 0.0f, 0.0f, 0.0f };
    ImuSample_t  s;
    TickType_t   last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10u);   /* 100 Hz */

    for (;;)
    {
        vTaskDelayUntil(&last, period);

        if (xQueuePeek(g_imu_latest_q, &s, 0) == pdTRUE)
        {
            gpio_put(PIN_EST_MARKER, 1);
            q = mahony_step(&s, q);
            (void)xQueueOverwrite(g_quat_latest_q, &q);
            gpio_put(PIN_EST_MARKER, 0);
        }
    }
}

/* ---- vDownsample: 10 Hz, format a line and post to gatekeeper -------- */

static void vDownsample(void *pv)
{
    (void)pv;

    Quaternion_t q;
    UartLine_t   line;
    TickType_t   last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(100u);  /* 10 Hz */

    for (;;)
    {
        vTaskDelayUntil(&last, period);

        if (xQueuePeek(g_quat_latest_q, &q, 0) == pdTRUE)
        {
            gpio_put(PIN_DS_MARKER, 1);

            const uint32_t drops = mpu6050_get_drop_count();
            (void)snprintf(line.line, sizeof(line.line),
                           "q=[%6.3f %6.3f %6.3f %6.3f] drops=%lu\n",
                           (double)q.q0, (double)q.q1,
                           (double)q.q2, (double)q.q3,
                           (unsigned long)drops);
            (void)xQueueSend(g_uart_queue, &line, 0);

            gpio_put(PIN_DS_MARKER, 0);
        }
    }
}

/* ---- vGatekeeper: serializes UART access ----------------------------- */

static void vGatekeeper(void *pv)
{
    (void)pv;

    UartLine_t line;

    for (;;)
    {
        if (xQueueReceive(g_uart_queue, &line, portMAX_DELAY) == pdTRUE)
        {
            uart_puts(UART_INSTANCE, line.line);
        }
    }
}

/* ---- Idle hook (defined here, called by the kernel) ------------------- */

void vApplicationIdleHook(void)
{
    /* Wait for next interrupt. Drops core power from ~85 mA to ~58 mA
     * at 125 MHz. ARMv6-M ARM A6.7.74. */
    __asm volatile ("wfi");
}

/* ---- Stack-overflow hook (defined here, called by the kernel) -------- */

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    /* Cannot use printf here (re-entrancy and stack state are bad).
     * Drive a GPIO high as the panic indicator and loop. */
    gpio_init((uint32_t)25u);
    gpio_set_dir((uint32_t)25u, GPIO_OUT);
    gpio_put((uint32_t)25u, 1);
    (void)pcTaskName;
    for (;;)
    {
        tight_loop_contents();
    }
}

/* ---- Setup ------------------------------------------------------------- */

static void configure_uart(void)
{
    uart_init(UART_INSTANCE, UART_BAUD_HZ);
    gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);
    uart_set_format(UART_INSTANCE, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_INSTANCE, true);
}

static void configure_marker_pins(void)
{
    const uint32_t pins[] = { PIN_ISR_MARKER, PIN_INGEST_MARKER,
                              PIN_EST_MARKER,  PIN_DS_MARKER };
    for (uint32_t i = 0u; i < (uint32_t)4u; ++i)
    {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 0);
    }
}

/* ---- main ------------------------------------------------------------- */

int main(void)
{
    /* USB stdio for diagnostic prints from main and the panic hook. */
    stdio_init_all();
    configure_marker_pins();
    configure_uart();

    /* Create queues and stream buffer before tasks. */
    g_mpu_stream    = xStreamBufferCreate(MPU_STREAM_BYTES,
                                          MPU_STREAM_TRIGGER);
    g_imu_latest_q  = xQueueCreate((UBaseType_t)1u, sizeof(ImuSample_t));
    g_quat_latest_q = xQueueCreate((UBaseType_t)1u, sizeof(Quaternion_t));
    g_uart_queue    = xQueueCreate(UART_QUEUE_LEN, sizeof(UartLine_t));

    configASSERT(g_mpu_stream    != NULL);
    configASSERT(g_imu_latest_q  != NULL);
    configASSERT(g_quat_latest_q != NULL);
    configASSERT(g_uart_queue    != NULL);

    /* Driver init BEFORE attaching the stream so we do not get a
     * spurious data-ready interrupt during register configuration. */
    if (!mpu6050_init())
    {
        printf("week07: MPU init failed; halting\n");
        for (;;)
        {
            tight_loop_contents();
        }
    }

    mpu6050_attach_stream(g_mpu_stream);

    /* Create tasks. */
    BaseType_t rc;
    rc = xTaskCreate(vIngest,      "ingest",  STACK_INGEST,
                     NULL, PRIO_INGEST,      NULL);
    configASSERT(rc == pdPASS);
    rc = xTaskCreate(vEstimator,   "estim",   STACK_ESTIMATOR,
                     NULL, PRIO_ESTIMATOR,   NULL);
    configASSERT(rc == pdPASS);
    rc = xTaskCreate(vDownsample,  "downsmp", STACK_DOWNSAMPLE,
                     NULL, PRIO_DOWNSAMPLE,  NULL);
    configASSERT(rc == pdPASS);
    rc = xTaskCreate(vGatekeeper,  "gate",    STACK_GATEKEEPER,
                     NULL, PRIO_GATEKEEPER,  NULL);
    configASSERT(rc == pdPASS);

    printf("week07: scheduler start\n");
    vTaskStartScheduler();

    for (;;)
    {
        tight_loop_contents();
    }
    return 0;
}
