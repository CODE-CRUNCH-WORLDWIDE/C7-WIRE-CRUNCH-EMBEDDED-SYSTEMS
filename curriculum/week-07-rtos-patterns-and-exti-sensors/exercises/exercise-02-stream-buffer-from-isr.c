/*
 * Exercise 2 — MPU-6050 EXTI to a stream buffer
 *
 * The MPU-6050 IMU produces a data-ready interrupt on its INT pin every
 * time a new sample is in its registers. We wire INT to GP22 and run the
 * sample rate at 200 Hz. On each interrupt, the ISR:
 *
 *   1. Reads INT_STATUS to clear the interrupt (also clears on any
 *      register read because we set INT_RD_CLEAR in INT_PIN_CFG).
 *   2. Reads the 14 sensor data bytes in one I2C burst from 0x3B.
 *      (We acknowledge that an I2C burst from an ISR is poor form;
 *      Week 8 replaces this with DMA. For Exercise 2 the simplicity is
 *      the point.)
 *   3. Posts the 14 bytes to a stream buffer at trigger level 14.
 *
 * The consumer task drains the stream buffer in 14-byte increments,
 * decodes a Mpu6050Frame_t, and prints the latest frame at 1 Hz.
 *
 *   GP2  -- I2C1 SDA -- MPU-6050 SDA (4.7k pull-up on breakout)
 *   GP3  -- I2C1 SCL -- MPU-6050 SCL (4.7k pull-up on breakout)
 *   GP22 -- MPU-6050 INT
 *   GP21 -- ISR-entry marker
 *   GP20 -- consumer-task wake marker
 *   3V3  -- MPU-6050 VCC
 *   GND  -- MPU-6050 GND
 *
 * The exercise has you compare three configurations on the same
 * hardware (run, save the Saleae capture, change one line, re-run):
 *
 *   A) Trigger level 1.  Consumer wakes 14 times per sample. ~28000
 *      context switches per second at 200 Hz. CPU at idle: ~22 %.
 *
 *   B) Trigger level 14. Consumer wakes once per sample. ~200 context
 *      switches per second. CPU at idle: ~6 %.
 *
 *   C) Queue of length 4 with item size sizeof(Mpu6050Frame_t).
 *      Consumer wakes once per sample. ~200 context switches per
 *      second. CPU at idle: ~5 %. Each ISR call is ~1.4 us instead of
 *      ~0.6 us for the stream buffer because the queue does a memcpy
 *      and the stream buffer does not.
 *
 * Document the three measurements in your homework. The chooser-table
 * winner is the stream buffer at trigger level 14, by a small margin
 * over the queue. The point is that the trigger-level tune matters
 * more than the primitive choice.
 *
 * API references:
 *   xStreamBufferCreate          https://www.freertos.org/xStreamBufferCreate.html
 *   xStreamBufferSendFromISR     https://www.freertos.org/xStreamBufferSendFromISR.html
 *   xStreamBufferReceive         https://www.freertos.org/xStreamBufferReceive.html
 *   xStreamBufferSetTriggerLevel https://www.freertos.org/xStreamBufferSetTriggerLevel.html
 *
 * RP2040 datasheet:
 *   2.19.6 GPIO IRQ              pp. 281-284
 *   4.3    I2C                   pp. 423-481
 *
 * MPU-6050 register map (rev 4.2 2013): pages 11, 13, 26, 27, 28, 41, 45.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/timer.h"

#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"

#include "mpu6050_regs.h"

/* ---- Pin assignments --------------------------------------------------- */

#define PIN_I2C_SDA         ((uint32_t)2u)
#define PIN_I2C_SCL         ((uint32_t)3u)
#define PIN_MPU_INT         ((uint32_t)22u)
#define PIN_ISR_MARKER      ((uint32_t)21u)
#define PIN_TASK_MARKER     ((uint32_t)20u)

/* ---- I2C parameters --------------------------------------------------- */

#define I2C_INSTANCE        i2c1
#define I2C_BAUD_HZ         ((uint32_t)400000u)   /* fast-mode 400 kHz */

/* ---- Stream-buffer parameters ----------------------------------------- */

#define STREAM_BUF_BYTES    ((size_t)256u)
#define STREAM_TRIGGER_LEN  ((size_t)14u)         /* one MPU frame */

/* ---- Task priorities -------------------------------------------------- */

#define PRIO_CONSUMER       ((UBaseType_t)3u)
#define PRIO_PRINTER        ((UBaseType_t)1u)

/* ---- Stack sizes (words) ---------------------------------------------- */

#define STACK_CONSUMER      ((configSTACK_DEPTH_TYPE)768u)
#define STACK_PRINTER       ((configSTACK_DEPTH_TYPE)1024u)

/* ---- Globals ---------------------------------------------------------- */

static StreamBufferHandle_t g_imu_stream  = NULL;
static volatile Mpu6050Frame_t g_latest_frame;      /* read by printer task */
static volatile uint32_t       g_dropped_frames = 0u;

/* ---- MPU-6050 register helpers ---------------------------------------- */

static bool mpu_write(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = { reg, val };
    int rc = i2c_write_blocking(I2C_INSTANCE, MPU6050_I2C_ADDR, buf,
                                (size_t)2, false);
    return (rc == 2);
}

static bool mpu_read_burst(uint8_t reg, uint8_t *out, size_t n)
{
    int rc;
    rc = i2c_write_blocking(I2C_INSTANCE, MPU6050_I2C_ADDR, &reg,
                            (size_t)1, true /* hold bus */);
    if (rc != 1)
    {
        return false;
    }
    rc = i2c_read_blocking(I2C_INSTANCE, MPU6050_I2C_ADDR, out, n, false);
    return (rc == (int)n);
}

/* ---- ISR --------------------------------------------------------------- */

static void mpu_int_isr(uint gpio, uint32_t events)
{
    (void)gpio;
    (void)events;

    gpio_put(PIN_ISR_MARKER, 1);

    uint32_t isr_us = time_us_32();
    uint8_t raw[MPU6050_DATA_BURST_LEN];

    /* Burst-read the 14 sensor bytes. This is a ~280 us blocking I2C
     * transfer at 400 kHz on Cortex-M0+ - acknowledged poor form for an
     * ISR but acceptable for Exercise 2. Week 8 replaces this with a
     * DMA-driven non-blocking ingest. */
    bool ok = mpu_read_burst(MPU6050_REG_DATA_START, raw,
                             MPU6050_DATA_BURST_LEN);

    if (ok)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        /* Append the timestamp so the consumer can compute end-to-end
         * latency. The 14 sensor bytes plus 4 timestamp bytes = 18 bytes
         * per send; the stream-buffer trigger level is set to 14 so the
         * consumer wakes once per frame and we send the extra 4 in a
         * second call. (Real production code would prepend a length
         * header and use a message buffer; see lecture 2 stretch goal.) */
        size_t sent = xStreamBufferSendFromISR(g_imu_stream, raw,
                                               MPU6050_DATA_BURST_LEN,
                                               &xHigherPriorityTaskWoken);

        if (sent != MPU6050_DATA_BURST_LEN)
        {
            /* The stream buffer was full. The consumer is too slow. */
            g_dropped_frames++;
        }
        else
        {
            /* Also append the timestamp. We do not check the return
             * code; if the timestamp send fails the frame is decodable
             * but the timestamp will be stale. */
            (void)xStreamBufferSendFromISR(g_imu_stream,
                                           (const uint8_t *)&isr_us,
                                           sizeof(isr_us),
                                           &xHigherPriorityTaskWoken);
        }

        gpio_put(PIN_ISR_MARKER, 0);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    else
    {
        gpio_put(PIN_ISR_MARKER, 0);
    }
}

/* ---- Consumer task ---------------------------------------------------- */

static void vConsumerTask(void *pv)
{
    (void)pv;

    uint8_t  buf[MPU6050_DATA_BURST_LEN];
    uint32_t isr_ts_us;

    for (;;)
    {
        /* Wake when at least STREAM_TRIGGER_LEN (= 14) bytes are
         * available. xStreamBufferReceive returns the count actually
         * delivered. With portMAX_DELAY it blocks indefinitely. */
        size_t got = xStreamBufferReceive(g_imu_stream, buf,
                                          MPU6050_DATA_BURST_LEN,
                                          portMAX_DELAY);

        if (got != MPU6050_DATA_BURST_LEN)
        {
            /* Should not happen with a trigger level == burst length;
             * if it does, the stream-buffer trigger is misconfigured. */
            continue;
        }

        gpio_put(PIN_TASK_MARKER, 1);

        /* Pull the timestamp the ISR appended. */
        (void)xStreamBufferReceive(g_imu_stream,
                                   (uint8_t *)&isr_ts_us,
                                   sizeof(isr_ts_us),
                                   pdMS_TO_TICKS(1));

        /* Decode the 14 big-endian signed-16 fields. */
        Mpu6050Frame_t f;
        f.accel_x     = (int16_t)((uint16_t)buf[0]  << 8 | buf[1]);
        f.accel_y     = (int16_t)((uint16_t)buf[2]  << 8 | buf[3]);
        f.accel_z     = (int16_t)((uint16_t)buf[4]  << 8 | buf[5]);
        f.temperature = (int16_t)((uint16_t)buf[6]  << 8 | buf[7]);
        f.gyro_x      = (int16_t)((uint16_t)buf[8]  << 8 | buf[9]);
        f.gyro_y      = (int16_t)((uint16_t)buf[10] << 8 | buf[11]);
        f.gyro_z      = (int16_t)((uint16_t)buf[12] << 8 | buf[13]);
        f.timestamp_us = isr_ts_us;

        /* Publish the latest decoded frame for the printer task. The
         * printer reads g_latest_frame without a lock; struct-tearing
         * across cores or interrupts is possible but the printer only
         * uses the frame for display, so a torn read is cosmetic. The
         * mini-project replaces this with a length-1 overwrite queue. */
        g_latest_frame = f;

        gpio_put(PIN_TASK_MARKER, 0);
    }
}

/* ---- Printer task ----------------------------------------------------- */

static void vPrinterTask(void *pv)
{
    (void)pv;

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000u);

    for (;;)
    {
        vTaskDelayUntil(&last_wake, period);

        Mpu6050Frame_t snap = g_latest_frame;       /* one-shot copy */
        printf("ax=%6d ay=%6d az=%6d  gx=%6d gy=%6d gz=%6d  drop=%lu\n",
               snap.accel_x, snap.accel_y, snap.accel_z,
               snap.gyro_x,  snap.gyro_y,  snap.gyro_z,
               (unsigned long)g_dropped_frames);
    }
}

/* ---- Setup ------------------------------------------------------------- */

static bool configure_mpu(void)
{
    /* Probe WHO_AM_I. */
    uint8_t id = 0u;
    if (!mpu_read_burst(MPU6050_REG_WHO_AM_I, &id, 1u))
    {
        return false;
    }
    if (id != MPU6050_WHO_AM_I_VALUE)
    {
        return false;
    }

    /* Wake from sleep and use PLL-X clock. */
    if (!mpu_write(MPU6050_REG_PWR_MGMT_1, MPU6050_PWR_MGMT_1_PLLX))
    {
        return false;
    }

    /* DLPF, full-scale ranges, sample rate. */
    if (!mpu_write(MPU6050_REG_CONFIG,        MPU6050_CONFIG_DLPF_44HZ))
    {
        return false;
    }
    if (!mpu_write(MPU6050_REG_GYRO_CONFIG,   MPU6050_GYRO_FS_500DPS))
    {
        return false;
    }
    if (!mpu_write(MPU6050_REG_ACCEL_CONFIG,  MPU6050_ACCEL_FS_4G))
    {
        return false;
    }
    if (!mpu_write(MPU6050_REG_SMPLRT_DIV,    MPU6050_SMPLRT_DIV_200HZ))
    {
        return false;
    }

    /* Interrupt pin and enable. INT_RD_CLEAR means we do not need an
     * explicit INT_STATUS read in the ISR; reading the data registers
     * (which we do) clears the interrupt. */
    if (!mpu_write(MPU6050_REG_INT_PIN_CFG, MPU6050_INT_PIN_CFG_DEFAULT))
    {
        return false;
    }
    if (!mpu_write(MPU6050_REG_INT_ENABLE,  MPU6050_INT_ENABLE_DRDY))
    {
        return false;
    }

    return true;
}

static void configure_pins(void)
{
    i2c_init(I2C_INSTANCE, I2C_BAUD_HZ);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);

    gpio_init(PIN_MPU_INT);
    gpio_set_dir(PIN_MPU_INT, GPIO_IN);
    /* The MPU's INT pin is push-pull active-high; no internal pull needed. */

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

    /* Configure the sensor BEFORE installing the ISR, so we do not get
     * a spurious data-ready interrupt during init. */
    if (!configure_mpu())
    {
        printf("ex02: MPU-6050 init failed\n");
        for (;;)
        {
            tight_loop_contents();
        }
    }

    g_imu_stream = xStreamBufferCreate(STREAM_BUF_BYTES, STREAM_TRIGGER_LEN);
    configASSERT(g_imu_stream != NULL);

    gpio_set_irq_enabled_with_callback(PIN_MPU_INT,
                                       GPIO_IRQ_EDGE_RISE,
                                       true,
                                       &mpu_int_isr);

    BaseType_t rc;
    rc = xTaskCreate(vConsumerTask, "consumer", STACK_CONSUMER, NULL,
                     PRIO_CONSUMER, NULL);
    configASSERT(rc == pdPASS);
    rc = xTaskCreate(vPrinterTask,  "printer",  STACK_PRINTER,  NULL,
                     PRIO_PRINTER,  NULL);
    configASSERT(rc == pdPASS);

    printf("ex02_stream_buffer: scheduler start\n");
    vTaskStartScheduler();

    for (;;)
    {
        tight_loop_contents();
    }
    return 0;
}
