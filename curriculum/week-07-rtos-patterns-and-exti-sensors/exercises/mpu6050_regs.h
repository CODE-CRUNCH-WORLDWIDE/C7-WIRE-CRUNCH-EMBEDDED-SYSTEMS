/*
 * mpu6050_regs.h — register-address header for the MPU-6050 IMU
 *
 * Shared between Exercise 2 and the Week 7 mini-project. The register
 * numbers and bit definitions are reproduced from the InvenSense
 * "MPU-6000/MPU-6050 Register Map and Descriptions" (rev 4.2, 2013),
 * pages cited inline. The MPU-6050 lives at 7-bit I2C address 0x68
 * (AD0 = GND) or 0x69 (AD0 = VCC); the GY-521 breakout we use leaves
 * AD0 low, so we hard-code 0x68 here.
 *
 * Style notes:
 *   - All fields are fixed-width uint8_t / uint32_t per MISRA-C 2012
 *     rule 4.6 (avoid the basic types in favour of size-explicit aliases).
 *   - No magic numbers in user code; every register address and every
 *     bit mask has a symbolic name.
 */

#ifndef MPU6050_REGS_H
#define MPU6050_REGS_H

#include <stdint.h>

/* I2C address of the MPU-6050 on the GY-521 breakout (AD0 = GND). */
#define MPU6050_I2C_ADDR            ((uint8_t)0x68u)

/* ---- Register addresses ------------------------------------------------ */
/* Page numbers refer to InvenSense register map rev 4.2 (2013).            */

/* SMPLRT_DIV — sample-rate divider (page 11).
 *   sample_rate_hz = gyro_output_rate_hz / (1 + SMPLRT_DIV)
 *   gyro_output_rate_hz = 1000 Hz when DLPF is enabled (DLPF_CFG = 1..6)
 *   For 200 Hz sample rate: SMPLRT_DIV = 4 -> 1000 / 5 = 200 Hz. */
#define MPU6050_REG_SMPLRT_DIV      ((uint8_t)0x19u)
#define MPU6050_SMPLRT_DIV_200HZ    ((uint8_t)0x04u)

/* CONFIG — DLPF and external sync (page 13).
 *   DLPF_CFG = 3 selects ~44 Hz accel / 42 Hz gyro bandwidth; appropriate
 *   for a 200 Hz sample rate (Nyquist-safe for the accel signal of
 *   interest, which is < 22 Hz for any vehicle attitude application). */
#define MPU6050_REG_CONFIG          ((uint8_t)0x1Au)
#define MPU6050_CONFIG_DLPF_44HZ    ((uint8_t)0x03u)

/* GYRO_CONFIG — full-scale range (page 14).
 *   FS_SEL bits 4:3. 00 = +/-250 dps, 01 = +/-500, 10 = +/-1000, 11 = +/-2000.
 *   We use +/-500 dps for general-purpose attitude work. */
#define MPU6050_REG_GYRO_CONFIG     ((uint8_t)0x1Bu)
#define MPU6050_GYRO_FS_500DPS      ((uint8_t)0x08u)

/* ACCEL_CONFIG — full-scale range (page 15).
 *   AFS_SEL bits 4:3. 00 = +/-2g, 01 = +/-4g, 10 = +/-8g, 11 = +/-16g. */
#define MPU6050_REG_ACCEL_CONFIG    ((uint8_t)0x1Cu)
#define MPU6050_ACCEL_FS_4G         ((uint8_t)0x08u)

/* INT_PIN_CFG — interrupt pin behaviour (page 26).
 *   bit 7 INT_LEVEL    (0 = active-high)
 *   bit 6 INT_OPEN     (0 = push-pull, 1 = open-drain)
 *   bit 5 LATCH_INT_EN (0 = 50 us pulse, 1 = held until cleared)
 *   bit 4 INT_RD_CLEAR (1 = any read clears the interrupt)
 *
 *   We use 0x10 = active-high push-pull 50us pulse clear-on-any-read.
 *   This matches a rising-edge GPIO interrupt on GP22 and means we do
 *   not need to read INT_STATUS explicitly to clear; reading the
 *   sensor-data burst (which we do anyway) clears it. */
#define MPU6050_REG_INT_PIN_CFG     ((uint8_t)0x37u)
#define MPU6050_INT_PIN_CFG_DEFAULT ((uint8_t)0x10u)

/* INT_ENABLE — interrupt enable (page 27).
 *   bit 0 DATA_RDY_EN  (1 = enable data-ready interrupt) */
#define MPU6050_REG_INT_ENABLE      ((uint8_t)0x38u)
#define MPU6050_INT_ENABLE_DRDY     ((uint8_t)0x01u)

/* INT_STATUS — read to identify source (page 28).
 *   bit 0 DATA_RDY_INT — 1 if the most recent edge was a data-ready event. */
#define MPU6050_REG_INT_STATUS      ((uint8_t)0x3Au)
#define MPU6050_INT_STATUS_DRDY     ((uint8_t)0x01u)

/* Sensor data registers (pages 28-31). 14 contiguous bytes:
 *   0x3B  ACCEL_XOUT_H
 *   0x3C  ACCEL_XOUT_L
 *   0x3D  ACCEL_YOUT_H
 *   0x3E  ACCEL_YOUT_L
 *   0x3F  ACCEL_ZOUT_H
 *   0x40  ACCEL_ZOUT_L
 *   0x41  TEMP_OUT_H
 *   0x42  TEMP_OUT_L
 *   0x43  GYRO_XOUT_H
 *   0x44  GYRO_XOUT_L
 *   0x45  GYRO_YOUT_H
 *   0x46  GYRO_YOUT_L
 *   0x47  GYRO_ZOUT_H
 *   0x48  GYRO_ZOUT_L
 * We read all 14 in one I2C burst starting at 0x3B. Each pair is
 * big-endian signed-16. */
#define MPU6050_REG_DATA_START      ((uint8_t)0x3Bu)
#define MPU6050_DATA_BURST_LEN      ((uint32_t)14u)

/* PWR_MGMT_1 — clock source and sleep (page 41).
 *   bit 6 SLEEP  (1 = device in sleep, 0 = active)
 *   bits 2:0 CLKSEL  (001 = PLL with X-axis gyro reference)
 *   We use 0x01 = active, PLL-X gyro clock (more stable than the 8 MHz
 *   internal oscillator). */
#define MPU6050_REG_PWR_MGMT_1      ((uint8_t)0x6Bu)
#define MPU6050_PWR_MGMT_1_PLLX     ((uint8_t)0x01u)

/* WHO_AM_I — read-only chip identity (page 45).
 *   Returns 0x68 on a real MPU-6050. Read at bringup to confirm wiring
 *   before sending any configuration write. */
#define MPU6050_REG_WHO_AM_I        ((uint8_t)0x75u)
#define MPU6050_WHO_AM_I_VALUE      ((uint8_t)0x68u)

/* ---- Decoded-frame layout --------------------------------------------- */

typedef struct
{
    int16_t  accel_x;       /* raw counts, big-endian decoded */
    int16_t  accel_y;
    int16_t  accel_z;
    int16_t  temperature;   /* raw counts; degrees C = (raw / 340.0) + 36.53 */
    int16_t  gyro_x;
    int16_t  gyro_y;
    int16_t  gyro_z;
    uint32_t timestamp_us;  /* time_us_32() at ISR entry */
} Mpu6050Frame_t;

#endif /* MPU6050_REGS_H */
