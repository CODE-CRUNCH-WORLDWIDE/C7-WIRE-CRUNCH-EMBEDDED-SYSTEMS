/*
 * mpu6050_driver.h — driver header for the Week 7 IMU EXTI pipeline
 *
 * The driver owns the MPU-6050 on i2c1 and the GP22 EXTI line. It
 * exposes:
 *
 *   - mpu6050_init()   — probe, configure registers, install ISR.
 *   - mpu6050_attach_stream(StreamBufferHandle_t) — the ISR posts
 *     14-byte raw frames plus a 4-byte ISR timestamp to this handle.
 *   - mpu6050_get_drop_count() — total frames dropped due to a full
 *     stream buffer (consumer too slow).
 *
 * The header is consumed by main.c and (in tests) by a mock module.
 *
 * Include guard, fixed-width types, and an external linkage discipline
 * (no static funcs declared here) per MISRA-C 2012 directive 4.6 and
 * rule 8.5.
 */

#ifndef MPU6050_DRIVER_H
#define MPU6050_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "stream_buffer.h"

/* ---- Configuration constants ------------------------------------------ */

/* Pins. Centralised here so both the driver and the main wiring use
 * the same numbers. Change in one place, recompile, re-wire. */
#define MPU6050_PIN_I2C_SDA     ((uint32_t)2u)
#define MPU6050_PIN_I2C_SCL     ((uint32_t)3u)
#define MPU6050_PIN_INT         ((uint32_t)22u)

/* I2C bus instance and baud. fast-mode 400 kHz. */
#define MPU6050_I2C_INSTANCE    i2c1
#define MPU6050_I2C_BAUD_HZ     ((uint32_t)400000u)

/* Bytes per raw frame on the stream: 14 sensor bytes plus a 4-byte
 * ISR-entry timestamp (uint32_t microseconds). */
#define MPU6050_FRAME_RAW_LEN   ((uint32_t)14u)
#define MPU6050_FRAME_TS_LEN    ((uint32_t)4u)
#define MPU6050_FRAME_TOTAL_LEN ((uint32_t)(MPU6050_FRAME_RAW_LEN + \
                                            MPU6050_FRAME_TS_LEN))

/* ---- Public API ------------------------------------------------------- */

/*
 * Initialise the MPU-6050 and configure the GP22 EXTI line.
 *
 * Returns true on successful WHO_AM_I probe and configuration write,
 * false otherwise. Call BEFORE attaching the stream buffer.
 *
 * Side effects: configures i2c1, configures GP22 as input, installs the
 * SDK's shared GPIO ISR if not already installed, enables EDGE_RISE on
 * GP22 with our callback.
 */
bool mpu6050_init(void);

/*
 * Attach a stream buffer to receive raw IMU frames from the ISR.
 *
 * The ISR will call xStreamBufferSendFromISR on this handle for each
 * data-ready interrupt. The handle MUST remain valid for the lifetime
 * of the program; it is typically created at boot in main.c.
 *
 * Pass NULL to detach (the ISR becomes a no-op).
 */
void mpu6050_attach_stream(StreamBufferHandle_t handle);

/*
 * Return the cumulative count of frames the ISR dropped because the
 * stream buffer was full. A non-zero value indicates the consumer
 * task is too slow or the stream buffer is too small.
 */
uint32_t mpu6050_get_drop_count(void);

#endif /* MPU6050_DRIVER_H */
