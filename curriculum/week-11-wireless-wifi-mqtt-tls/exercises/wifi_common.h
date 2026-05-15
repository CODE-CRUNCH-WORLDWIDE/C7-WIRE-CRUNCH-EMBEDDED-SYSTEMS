/*
 * wifi_common.h — Shared definitions for Week 11 exercises.
 *
 * This header is included by every exercise .c file in this folder.
 * It centralizes:
 *   - WiFi credentials (do NOT commit your real ones; the defaults below
 *     are placeholders that the build system overrides via -D flags).
 *   - The MQTT broker host/port pair we point at by default.
 *   - The retry-backoff state-machine enum used by exercises that
 *     reconnect on link loss.
 *   - The MQTT packet-type constants from MQTT 5 §2.1.2 (pp. 19-22).
 *   - The Variable Byte Integer encoder/decoder types from MQTT 5 §1.5.5.
 *   - Helper macros for byte-order and length-prefixed UTF-8 strings.
 *
 * Style: explicit-width integer types throughout; no magic numbers;
 * include-guarded. Compiles clean under -Wall -Wextra -Werror at -Os
 * on arm-none-eabi-gcc 13.x against the pico-sdk 1.5.1.
 */

#ifndef WIFI_COMMON_H
#define WIFI_COMMON_H

#include <stdint.h>
#include <stddef.h>

/*
 * Credentials. The build system passes -DWIFI_SSID=... and -DWIFI_PASSWORD=...
 * via add_compile_definitions in CMakeLists.txt; the defaults here are
 * placeholders for grep-ability. NEVER commit a real password.
 */
#ifndef WIFI_SSID
#define WIFI_SSID     "PLACEHOLDER_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "PLACEHOLDER_PASSWORD"
#endif

/*
 * Auth mode. We default to WPA2/WPA3 mixed which works on the broadest
 * range of home APs. If your AP is WPA2-only, change to
 * CYW43_AUTH_WPA2_AES_PSK; if WPA3-only, change to CYW43_AUTH_WPA3_SAE_AES_PSK.
 */
#ifndef WIFI_AUTH_MODE
#define WIFI_AUTH_MODE CYW43_AUTH_WPA3_WPA2_AES_PSK
#endif

/* Association timeout in milliseconds. 30 seconds is the SDK's default. */
#define WIFI_CONNECT_TIMEOUT_MS  ((uint32_t) 30000U)

/* Broker. We default to test.mosquitto.org over TLS. */
#ifndef MQTT_BROKER_HOST
#define MQTT_BROKER_HOST "test.mosquitto.org"
#endif
#ifndef MQTT_BROKER_PORT
#define MQTT_BROKER_PORT ((uint16_t) 8883U)  /* TLS port */
#endif

/* Plain-TCP broker port for Exercise 2's local echo test. */
#define ECHO_SERVER_PORT  ((uint16_t) 7U)

/*
 * MQTT 5 packet type identifiers, high-nibble of the first fixed-header
 * byte. Source: MQTT 5.0 OASIS Standard, Table 2-1, p. 21.
 */
typedef enum {
    MQTT_PT_CONNECT     = (uint8_t) 0x10U,
    MQTT_PT_CONNACK     = (uint8_t) 0x20U,
    MQTT_PT_PUBLISH     = (uint8_t) 0x30U,
    MQTT_PT_PUBACK      = (uint8_t) 0x40U,
    MQTT_PT_PUBREC      = (uint8_t) 0x50U,
    MQTT_PT_PUBREL      = (uint8_t) 0x62U, /* flags = 0010 */
    MQTT_PT_PUBCOMP     = (uint8_t) 0x70U,
    MQTT_PT_SUBSCRIBE   = (uint8_t) 0x82U, /* flags = 0010 */
    MQTT_PT_SUBACK      = (uint8_t) 0x90U,
    MQTT_PT_UNSUBSCRIBE = (uint8_t) 0xA2U, /* flags = 0010 */
    MQTT_PT_UNSUBACK    = (uint8_t) 0xB0U,
    MQTT_PT_PINGREQ     = (uint8_t) 0xC0U,
    MQTT_PT_PINGRESP    = (uint8_t) 0xD0U,
    MQTT_PT_DISCONNECT  = (uint8_t) 0xE0U,
    MQTT_PT_AUTH        = (uint8_t) 0xF0U
} mqtt_packet_type_t;

/* MQTT 5 protocol level byte (CONNECT variable header). */
#define MQTT_PROTOCOL_LEVEL_5  ((uint8_t) 0x05U)

/* MQTT Variable Byte Integer maximum encodable value. Spec §1.5.5. */
#define MQTT_VBI_MAX_VALUE  ((uint32_t) 268435455U)
#define MQTT_VBI_MAX_BYTES  ((uint8_t) 4U)

/* Encoded VBI: up to 4 bytes plus a byte count. */
typedef struct {
    uint8_t bytes[4];
    uint8_t count;
} mqtt_vbi_encoded_t;

/* Decoded VBI: the value, the number of bytes consumed, and an error flag. */
typedef struct {
    uint32_t value;
    uint8_t  count;
    int8_t   error;  /* 0 on success, negative on malformed input */
} mqtt_vbi_decoded_t;

/*
 * Connect-flags bit positions (MQTT 5 §3.1.2.3, p. 41). The flags byte
 * sits at offset 7 of the CONNECT variable header.
 */
#define MQTT_CONNECT_FLAG_USERNAME      ((uint8_t) (1U << 7U))
#define MQTT_CONNECT_FLAG_PASSWORD      ((uint8_t) (1U << 6U))
#define MQTT_CONNECT_FLAG_WILL_RETAIN   ((uint8_t) (1U << 5U))
#define MQTT_CONNECT_FLAG_WILL_QOS_MASK ((uint8_t) (3U << 3U))
#define MQTT_CONNECT_FLAG_WILL          ((uint8_t) (1U << 2U))
#define MQTT_CONNECT_FLAG_CLEAN_START   ((uint8_t) (1U << 1U))

/*
 * Subset of CONNACK Reason Codes from MQTT 5 §3.2.2.2 (pp. 56-58).
 * The mini-project's retry-backoff state machine treats codes 136, 137,
 * and 159 as transient (retry); 133, 134, 135, and 138 as fatal.
 */
typedef enum {
    MQTT_RC_SUCCESS                    = (uint8_t) 0U,
    MQTT_RC_UNSPECIFIED_ERROR          = (uint8_t) 128U,
    MQTT_RC_PROTOCOL_ERROR             = (uint8_t) 130U,
    MQTT_RC_UNSUPPORTED_VERSION        = (uint8_t) 132U,
    MQTT_RC_CLIENT_ID_NOT_VALID        = (uint8_t) 133U,
    MQTT_RC_BAD_USERNAME_PASSWORD      = (uint8_t) 134U,
    MQTT_RC_NOT_AUTHORIZED             = (uint8_t) 135U,
    MQTT_RC_SERVER_UNAVAILABLE         = (uint8_t) 136U,
    MQTT_RC_SERVER_BUSY                = (uint8_t) 137U,
    MQTT_RC_BANNED                     = (uint8_t) 138U,
    MQTT_RC_USE_ANOTHER_SERVER         = (uint8_t) 142U,
    MQTT_RC_TOPIC_NAME_INVALID         = (uint8_t) 144U,
    MQTT_RC_CONNECTION_RATE_EXCEEDED   = (uint8_t) 159U
} mqtt_connack_reason_t;

/*
 * Retry-backoff state machine. Exhaustive: every reachable runtime
 * condition maps to exactly one state.
 */
typedef enum {
    NET_STATE_BOOT                 = 0,
    NET_STATE_WIFI_DOWN            = 1,
    NET_STATE_WIFI_UP_BROKER_DOWN  = 2,
    NET_STATE_CONNECTED            = 3,
    NET_STATE_BACKOFF              = 4
} net_state_t;

/* Backoff parameters. The mini-project uses these exact values. */
#define NET_BACKOFF_INITIAL_MS  ((uint32_t) 1000U)
#define NET_BACKOFF_MAX_MS      ((uint32_t) 60000U)
#define NET_BACKOFF_JITTER_PCT  ((uint8_t) 25U)

/*
 * Topic strings. The {id} placeholder is replaced at runtime with the
 * device's unique-id-derived suffix (3 hex bytes from the RP2040's
 * 64-bit board ID — see pico/unique_id.h).
 */
#define MQTT_TOPIC_TELEMETRY_PREFIX  "cc7/devices/"
#define MQTT_TOPIC_TELEMETRY_SUFFIX  "/telemetry"
#define MQTT_TOPIC_CMD_SUFFIX        "/cmd"

/* Default MQTT keep-alive in seconds. */
#define MQTT_KEEPALIVE_SECONDS  ((uint16_t) 60U)

/* Publish cadence. */
#define PUBLISH_INTERVAL_MS  ((uint32_t) 10000U)

/*
 * Helper: append a 2-byte big-endian length prefix followed by the bytes
 * of a UTF-8 string to *buf, advancing *cursor by the bytes written.
 * Returns the number of bytes written, or a negative value if it
 * would have overflowed `buflen`.
 */
static inline int32_t mqtt_buf_put_utf8(uint8_t *buf, uint32_t *cursor,
                                        uint32_t buflen, const char *str,
                                        uint32_t strlen_bytes) {
    if ((*cursor + 2U + strlen_bytes) > buflen) {
        return (int32_t) -1;
    }
    buf[*cursor]     = (uint8_t) ((strlen_bytes >> 8U) & 0xFFU);
    buf[*cursor + 1U] = (uint8_t) (strlen_bytes & 0xFFU);
    *cursor += 2U;
    for (uint32_t i = 0U; i < strlen_bytes; i++) {
        buf[*cursor + i] = (uint8_t) str[i];
    }
    *cursor += strlen_bytes;
    return (int32_t) (2U + strlen_bytes);
}

/*
 * Encode a Variable Byte Integer (MQTT 5 §1.5.5). Maximum encodable
 * value 268435455; any larger input returns count=0 and the bytes
 * field is undefined.
 */
static inline mqtt_vbi_encoded_t mqtt_vbi_encode(uint32_t value) {
    mqtt_vbi_encoded_t out;
    out.count = 0U;
    for (uint8_t i = 0U; i < 4U; i++) {
        out.bytes[i] = 0U;
    }
    if (value > MQTT_VBI_MAX_VALUE) {
        return out;
    }
    do {
        uint8_t b = (uint8_t) (value & 0x7FU);
        value >>= 7U;
        if (value > 0U) {
            b = (uint8_t) (b | 0x80U);
        }
        out.bytes[out.count] = b;
        out.count++;
    } while (value > 0U && out.count < MQTT_VBI_MAX_BYTES);
    return out;
}

/*
 * Decode a Variable Byte Integer. On success, .error == 0 and .count
 * holds the number of bytes consumed; on malformed input (4 bytes
 * seen without a continuation-bit-clear), .error == -1.
 */
static inline mqtt_vbi_decoded_t mqtt_vbi_decode(const uint8_t *buf,
                                                  uint32_t buflen) {
    mqtt_vbi_decoded_t out;
    out.value = 0U;
    out.count = 0U;
    out.error = 0;
    uint32_t multiplier = 1U;
    uint8_t  index = 0U;
    while (index < MQTT_VBI_MAX_BYTES && (uint32_t) index < buflen) {
        uint8_t b = buf[index];
        out.value += ((uint32_t) (b & 0x7FU)) * multiplier;
        index++;
        if ((b & 0x80U) == 0U) {
            out.count = index;
            return out;
        }
        multiplier *= 128U;
    }
    out.error = (int8_t) -1;
    return out;
}

#endif /* WIFI_COMMON_H */
