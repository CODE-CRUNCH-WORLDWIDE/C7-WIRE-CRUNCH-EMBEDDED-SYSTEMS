/*
 * exercise-03-mqtt5-encode-decode.c — Round-trip every Variable Byte
 * Integer test vector from MQTT 5.0 §1.5.5 (p. 16), then encode a
 * minimal CONNECT packet, then decode a hand-built CONNACK byte
 * sequence. Compares actual bytes to expected bytes and prints
 * PASS/FAIL per case.
 *
 * This exercise runs on the Pico as a one-shot self-test program;
 * it does NOT use WiFi. It is the encoder/decoder you will later
 * link into the mini-project.
 *
 * Build:
 *   target_link_libraries(exercise-03 pico_stdlib)
 *
 * Expected output (UART):
 *
 *   [test] mqtt 5 encode/decode self-test
 *   [vbi]  enc(0)         -> 00                : PASS
 *   [vbi]  enc(127)       -> 7F                : PASS
 *   [vbi]  enc(128)       -> 80 01             : PASS
 *   [vbi]  enc(16383)     -> FF 7F             : PASS
 *   [vbi]  enc(16384)     -> 80 80 01          : PASS
 *   [vbi]  enc(2097151)   -> FF FF 7F          : PASS
 *   [vbi]  enc(268435455) -> FF FF FF 7F       : PASS
 *   [vbi]  dec(00)         -> 0                : PASS
 *   [vbi]  dec(80 01)      -> 128              : PASS
 *   ...
 *   [connect] encoded 24 bytes: 10 16 00 04 4D 51 54 54 05 02 00 3C ...
 *   [connect] expected matches : PASS
 *   [connack] decoded reason=0 (Success)      : PASS
 *   [test] all 14 cases PASS
 *
 * Cite: MQTT 5.0 §1.5.5 Variable Byte Integer; §3.1 CONNECT; §3.2 CONNACK.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "pico/stdlib.h"
#include "wifi_common.h"

/* Track test outcomes. */
static uint32_t g_pass_count;
static uint32_t g_fail_count;

/* Format a byte buffer as space-separated hex into a caller-provided str. */
static void hexstr(const uint8_t *buf, uint32_t len, char *out, uint32_t outlen) {
    uint32_t cursor = 0U;
    for (uint32_t i = 0U; i < len; i++) {
        if (cursor + 3U >= outlen) {
            break;
        }
        (void) snprintf(out + cursor, outlen - cursor,
                        (i + 1U == len) ? "%02X" : "%02X ",
                        (unsigned) buf[i]);
        cursor += (i + 1U == len) ? 2U : 3U;
    }
    out[cursor < outlen ? cursor : outlen - 1U] = '\0';
}

/* Verify one VBI encoder case. */
static void test_vbi_encode(uint32_t value, const uint8_t *expected,
                            uint8_t expected_count) {
    mqtt_vbi_encoded_t enc = mqtt_vbi_encode(value);
    int ok = (enc.count == expected_count) ? 1 : 0;
    if (ok == 1) {
        for (uint8_t i = 0U; i < expected_count; i++) {
            if (enc.bytes[i] != expected[i]) {
                ok = 0;
                break;
            }
        }
    }
    char hex[20] = {0};
    hexstr(enc.bytes, enc.count, hex, sizeof hex);
    (void) printf("[vbi]  enc(%-10lu) -> %-18s : %s\n",
                  (unsigned long) value, hex, (ok == 1) ? "PASS" : "FAIL");
    if (ok == 1) { g_pass_count++; } else { g_fail_count++; }
}

/* Verify one VBI decoder case. */
static void test_vbi_decode(const uint8_t *buf, uint32_t buflen,
                            uint32_t expected_value, uint8_t expected_count) {
    mqtt_vbi_decoded_t dec = mqtt_vbi_decode(buf, buflen);
    int ok = (dec.error == 0 &&
              dec.value == expected_value &&
              dec.count == expected_count) ? 1 : 0;
    char hex[20] = {0};
    hexstr(buf, buflen, hex, sizeof hex);
    (void) printf("[vbi]  dec(%-12s) -> %-10lu : %s\n",
                  hex, (unsigned long) dec.value,
                  (ok == 1) ? "PASS" : "FAIL");
    if (ok == 1) { g_pass_count++; } else { g_fail_count++; }
}

/*
 * Encode a minimal CONNECT: clean-start, no will, no auth, keep-alive 60,
 * client-id "cc7-pico-0001". Writes into *out, returns bytes written.
 */
static uint32_t encode_minimal_connect(uint8_t *out, uint32_t outlen,
                                       const char *client_id) {
    if (outlen < 64U) {
        return 0U;
    }

    /* Variable header. */
    uint8_t  vh[64];
    uint32_t vh_cursor = 0U;

    /* Protocol Name "MQTT" length-prefixed. */
    (void) mqtt_buf_put_utf8(vh, &vh_cursor, sizeof vh, "MQTT", 4U);

    /* Protocol Level. */
    vh[vh_cursor] = MQTT_PROTOCOL_LEVEL_5;
    vh_cursor++;

    /* Connect Flags: Clean Start only. */
    vh[vh_cursor] = MQTT_CONNECT_FLAG_CLEAN_START;
    vh_cursor++;

    /* Keep-alive 60 s (big-endian). */
    uint16_t ka = (uint16_t) MQTT_KEEPALIVE_SECONDS;
    vh[vh_cursor]     = (uint8_t) ((ka >> 8U) & 0xFFU);
    vh[vh_cursor + 1U] = (uint8_t) (ka & 0xFFU);
    vh_cursor += 2U;

    /* Properties Length 0 (VBI). */
    vh[vh_cursor] = (uint8_t) 0U;
    vh_cursor++;

    /* Payload: Client Identifier. */
    uint32_t cid_len = (uint32_t) strlen(client_id);
    (void) mqtt_buf_put_utf8(vh, &vh_cursor, sizeof vh, client_id, cid_len);

    /* Fixed header: type byte + VBI of remaining length. */
    uint32_t remaining = vh_cursor;
    mqtt_vbi_encoded_t rl = mqtt_vbi_encode(remaining);
    if (rl.count == 0U) {
        return 0U;
    }

    uint32_t cursor = 0U;
    out[cursor] = (uint8_t) MQTT_PT_CONNECT;
    cursor++;
    for (uint8_t i = 0U; i < rl.count; i++) {
        out[cursor] = rl.bytes[i];
        cursor++;
    }
    for (uint32_t i = 0U; i < vh_cursor; i++) {
        if (cursor >= outlen) {
            return 0U;
        }
        out[cursor] = vh[i];
        cursor++;
    }
    return cursor;
}

/*
 * Verify our encoded CONNECT against the byte-perfect expected output
 * from the lecture-3 walkthrough.
 */
static void test_connect_encoding(void) {
    static const uint8_t expected[] = {
        0x10U, 0x18U,                                 /* type, remaining-length 24 */
        0x00U, 0x04U, 0x4DU, 0x51U, 0x54U, 0x54U,     /* "MQTT" */
        0x05U,                                        /* protocol level */
        0x02U,                                        /* connect flags: clean-start */
        0x00U, 0x3CU,                                 /* keep-alive 60 */
        0x00U,                                        /* properties length 0 */
        0x00U, 0x0DU,                                 /* client-id length 13 */
        0x63U, 0x63U, 0x37U, 0x2DU,                   /* "cc7-" */
        0x70U, 0x69U, 0x63U, 0x6FU,                   /* "pico" */
        0x2DU, 0x30U, 0x30U, 0x30U, 0x31U             /* "-0001" */
    };

    uint8_t  encoded[64] = {0};
    uint32_t n = encode_minimal_connect(encoded, sizeof encoded,
                                         "cc7-pico-0001");

    char hex[200] = {0};
    hexstr(encoded, n, hex, sizeof hex);
    (void) printf("[connect] encoded %lu bytes: %s\n",
                  (unsigned long) n, hex);

    int ok = (n == sizeof expected) ? 1 : 0;
    if (ok == 1) {
        for (uint32_t i = 0U; i < n; i++) {
            if (encoded[i] != expected[i]) {
                ok = 0;
                break;
            }
        }
    }
    (void) printf("[connect] expected matches : %s\n",
                  (ok == 1) ? "PASS" : "FAIL");
    if (ok == 1) { g_pass_count++; } else { g_fail_count++; }
}

/*
 * Decode a hand-built CONNACK byte sequence (type byte 0x20, remaining
 * length 3, connack flags 0, reason code 0, properties length 0).
 * Extract the reason code and verify.
 */
static void test_connack_decoding(void) {
    static const uint8_t bytes[] = { 0x20U, 0x03U, 0x00U, 0x00U, 0x00U };

    /* Verify type byte. */
    int ok = ((bytes[0] & 0xF0U) == MQTT_PT_CONNACK) ? 1 : 0;

    /* Decode remaining-length VBI starting at offset 1. */
    mqtt_vbi_decoded_t rl = mqtt_vbi_decode(&bytes[1], sizeof bytes - 1U);
    if (ok == 1 && (rl.error != 0 || rl.value != 3U)) {
        ok = 0;
    }

    /* The variable header is at offset 1 + rl.count. */
    uint32_t vh_off = 1U + rl.count;

    /* CONNACK reason code is the second byte of the variable header. */
    uint8_t reason = (vh_off + 1U < sizeof bytes) ? bytes[vh_off + 1U] : 0xFFU;
    if (ok == 1 && reason != (uint8_t) MQTT_RC_SUCCESS) {
        ok = 0;
    }

    (void) printf("[connack] decoded reason=%u (%s)        : %s\n",
                  (unsigned) reason,
                  (reason == (uint8_t) MQTT_RC_SUCCESS) ? "Success" : "?",
                  (ok == 1) ? "PASS" : "FAIL");
    if (ok == 1) { g_pass_count++; } else { g_fail_count++; }
}

int main(void) {
    (void) stdio_init_all();
    sleep_ms(2000U);

    (void) printf("[test] mqtt 5 encode/decode self-test\n");

    g_pass_count = 0U;
    g_fail_count = 0U;

    /* VBI encoder vectors (from MQTT 5 spec Table 1-1 plus the boundaries). */
    {
        static const uint8_t e0[]   = { 0x00U };
        static const uint8_t e127[] = { 0x7FU };
        static const uint8_t e128[] = { 0x80U, 0x01U };
        static const uint8_t eF7F[] = { 0xFFU, 0x7FU };
        static const uint8_t e3[]   = { 0x80U, 0x80U, 0x01U };
        static const uint8_t e3M[]  = { 0xFFU, 0xFFU, 0x7FU };
        static const uint8_t e4M[]  = { 0xFFU, 0xFFU, 0xFFU, 0x7FU };

        test_vbi_encode(0U,         e0,   (uint8_t) 1U);
        test_vbi_encode(127U,       e127, (uint8_t) 1U);
        test_vbi_encode(128U,       e128, (uint8_t) 2U);
        test_vbi_encode(16383U,     eF7F, (uint8_t) 2U);
        test_vbi_encode(16384U,     e3,   (uint8_t) 3U);
        test_vbi_encode(2097151U,   e3M,  (uint8_t) 3U);
        test_vbi_encode(268435455U, e4M,  (uint8_t) 4U);
    }

    /* VBI decoder vectors — same data in the other direction. */
    {
        static const uint8_t d0[]   = { 0x00U };
        static const uint8_t d128[] = { 0x80U, 0x01U };
        static const uint8_t d16k[] = { 0x80U, 0x80U, 0x01U };
        static const uint8_t d4M[]  = { 0xFFU, 0xFFU, 0xFFU, 0x7FU };

        test_vbi_decode(d0,   1U, 0U,         (uint8_t) 1U);
        test_vbi_decode(d128, 2U, 128U,       (uint8_t) 2U);
        test_vbi_decode(d16k, 3U, 16384U,     (uint8_t) 3U);
        test_vbi_decode(d4M,  4U, 268435455U, (uint8_t) 4U);
    }

    /* CONNECT encoder. */
    test_connect_encoding();

    /* CONNACK decoder. */
    test_connack_decoding();

    if (g_fail_count == 0U) {
        (void) printf("[test] all %lu cases PASS\n",
                      (unsigned long) g_pass_count);
    } else {
        (void) printf("[test] %lu PASS, %lu FAIL\n",
                      (unsigned long) g_pass_count,
                      (unsigned long) g_fail_count);
    }

    /* Idle. */
    for (;;) {
        sleep_ms(1000U);
    }
}
