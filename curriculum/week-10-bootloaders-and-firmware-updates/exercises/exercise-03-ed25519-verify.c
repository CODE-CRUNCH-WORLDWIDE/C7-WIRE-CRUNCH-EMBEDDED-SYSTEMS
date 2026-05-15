/*
 * exercise-03-ed25519-verify.c — Verify an Ed25519 signature against a known
 * test vector from RFC 8032 §7.1.
 *
 * The verify function `ed25519_sign_open` is provided by the ed25519-donna
 * reference implementation (public domain), included in the mini-project's
 * source tree. For this exercise we declare it extern and link it at build
 * time; the test vectors are hardcoded.
 *
 * The first RFC 8032 §7.1 test vector:
 *
 *   SECRET KEY (private, 32 bytes):
 *     9d61b19deffd5a60ba844af492ec2cc4 4449c5697b326919703bac031cae7f60
 *   PUBLIC KEY (32 bytes):
 *     d75a980182b10ab7d54bfed3c964073a 0ee172f3daa62325af021a68f707511a
 *   MESSAGE (empty):
 *     (zero bytes)
 *   SIGNATURE (64 bytes):
 *     e5564300c360ac729086e2cc806e828a 84877f1eb8e5d974d873e06522490155
 *     5fb8821590a33bacc61e39701cf9b46b d25bf5f0595bbe24655141438e7a100b
 *
 * The exercise extracts these constants, calls ed25519_sign_open, and
 * verifies the result is 0 (success). Then it deliberately flips one bit
 * of the signature and verifies the result is non-zero (failure).
 *
 * Build (on a Linux/macOS host for fast iteration):
 *   cc -std=c11 -Wall -Wextra -O2 -DED25519_REFHASH -DED25519_FORCE_32BIT \
 *      -o ed25519test exercise-03-ed25519-verify.c \
 *      path/to/ed25519-donna/ed25519.c
 *
 * Or on the Pico via the mini-project CMakeLists.
 *
 * Citations:
 *   - RFC 8032 §7.1 (Test Vectors), pp. 24-25.
 *   - ed25519-donna, https://github.com/floodyberry/ed25519-donna.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "bootloader_common.h"

/* The ed25519-donna verify function. We declare it extern; the project's
   build links the donna implementation in. The signature matches the
   donna header's `ed25519_sign_open`. */
extern int ed25519_sign_open(const unsigned char *m,
                             size_t mlen,
                             const unsigned char *pk,
                             const unsigned char *RS);

/* RFC 8032 §7.1 test vector #1: empty message. */
static const uint8_t test_public_key[CCF_PUBKEY_SIZE] = {
    0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7,
    0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
    0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25,
    0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a,
};

static const uint8_t test_signature[CCF_SIGNATURE_SIZE] = {
    0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72,
    0x90, 0x86, 0xe2, 0xcc, 0x80, 0x6e, 0x82, 0x8a,
    0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5, 0xd9, 0x74,
    0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55,
    0x5f, 0xb8, 0x82, 0x15, 0x90, 0xa3, 0x3b, 0xac,
    0xc6, 0x1e, 0x39, 0x70, 0x1c, 0xf9, 0xb4, 0x6b,
    0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe, 0x24,
    0x65, 0x51, 0x41, 0x43, 0x8e, 0x7a, 0x10, 0x0b,
};

/* The message is empty for test vector #1. */
static const uint8_t test_message[1] = { 0u };  /* unused; size = 0 */

/* RFC 8032 §7.1 test vector #2: 1-byte message. */
static const uint8_t test2_public_key[CCF_PUBKEY_SIZE] = {
    0x3d, 0x40, 0x17, 0xc3, 0xe8, 0x43, 0x89, 0x5a,
    0x92, 0xb7, 0x0a, 0xa7, 0x4d, 0x1b, 0x7e, 0xbc,
    0x9c, 0x98, 0x2c, 0xcf, 0x2e, 0xc4, 0x96, 0x8c,
    0xc0, 0xcd, 0x55, 0xf1, 0x2a, 0xf4, 0x66, 0x0c,
};

static const uint8_t test2_message[1] = { 0x72 };

static const uint8_t test2_signature[CCF_SIGNATURE_SIZE] = {
    0x92, 0xa0, 0x09, 0xa9, 0xf0, 0xd4, 0xca, 0xb8,
    0x72, 0x0e, 0x82, 0x0b, 0x5f, 0x64, 0x25, 0x40,
    0xa2, 0xb2, 0x7b, 0x54, 0x16, 0x50, 0x3f, 0x8f,
    0xb3, 0x76, 0x22, 0x23, 0xeb, 0xdb, 0x69, 0xda,
    0x08, 0x5a, 0xc1, 0xe4, 0x3e, 0x15, 0x99, 0x6e,
    0x45, 0x8f, 0x36, 0x13, 0xd0, 0xf1, 0x1d, 0x8c,
    0x38, 0x7b, 0x2e, 0xae, 0xb4, 0x30, 0x2a, 0xee,
    0xb0, 0x0d, 0x29, 0x16, 0x12, 0xbb, 0x0c, 0x00,
};

/* -------------------------------------------------------------------------
 * Helper: print a buffer in hex.
 * ----------------------------------------------------------------------- */

static void print_hex(const uint8_t *buf, size_t length, const char *label) {
    printf("  %s (%zu bytes): ", label, length);
    for (size_t i = 0u; i < length; i++) {
        printf("%02x", buf[i]);
        if (i + 1u < length && (i + 1u) % 16u == 0u) {
            printf("\n    ");
        }
    }
    printf("\n");
}

/* -------------------------------------------------------------------------
 * Single test case.
 * ----------------------------------------------------------------------- */

typedef struct {
    const char *name;
    const uint8_t *public_key;
    const uint8_t *message;
    size_t message_len;
    const uint8_t *signature;
    int expected_result;  /* 0 = should accept */
} test_case_t;

static int run_test(const test_case_t *tc) {
    printf("--- %s ---\n", tc->name);
    print_hex(tc->public_key, CCF_PUBKEY_SIZE, "public key");
    if (tc->message_len > 0u && tc->message_len <= 16u) {
        print_hex(tc->message, tc->message_len, "message");
    } else if (tc->message_len == 0u) {
        printf("  message: (empty)\n");
    } else {
        printf("  message: (%zu bytes, not printed)\n", tc->message_len);
    }
    print_hex(tc->signature, CCF_SIGNATURE_SIZE, "signature");

    int rv = ed25519_sign_open(tc->message,
                               tc->message_len,
                               tc->public_key,
                               tc->signature);
    int passed = (rv == tc->expected_result) ? 1 : 0;

    printf("  result: %d (expected %d) -> %s\n",
           rv, tc->expected_result, passed ? "PASS" : "FAIL");
    printf("\n");
    return passed;
}

/* -------------------------------------------------------------------------
 * Main: run three tests — two known-good vectors and one tampered.
 * ----------------------------------------------------------------------- */

int main(void) {
    /* Tampered signature: take the good signature for vector #1 and flip
       one bit in the middle. */
    uint8_t tampered_signature[CCF_SIGNATURE_SIZE];
    memcpy(tampered_signature, test_signature, CCF_SIGNATURE_SIZE);
    tampered_signature[32] ^= 0x01u;  /* flip the low bit of byte 32 */

    test_case_t tests[] = {
        {
            .name = "RFC 8032 §7.1 Test Vector #1 (empty message)",
            .public_key = test_public_key,
            .message = test_message,
            .message_len = 0u,
            .signature = test_signature,
            .expected_result = 0,
        },
        {
            .name = "RFC 8032 §7.1 Test Vector #2 (1-byte message 0x72)",
            .public_key = test2_public_key,
            .message = test2_message,
            .message_len = 1u,
            .signature = test2_signature,
            .expected_result = 0,
        },
        {
            .name = "Tampered signature (bit 0 of byte 32 flipped)",
            .public_key = test_public_key,
            .message = test_message,
            .message_len = 0u,
            .signature = tampered_signature,
            .expected_result = -1,  /* donna returns non-zero on failure */
        },
    };

    const size_t test_count = sizeof(tests) / sizeof(tests[0]);
    int total_passed = 0;
    for (size_t i = 0u; i < test_count; i++) {
        /* For the tampered case, "expected" is just "non-zero"; we treat
           any non-zero rv as a match for expected_result == -1. */
        if (tests[i].expected_result == -1) {
            int rv = ed25519_sign_open(tests[i].message,
                                       tests[i].message_len,
                                       tests[i].public_key,
                                       tests[i].signature);
            printf("--- %s ---\n", tests[i].name);
            printf("  result: %d (expected: non-zero) -> %s\n",
                   rv, (rv != 0) ? "PASS" : "FAIL");
            printf("\n");
            if (rv != 0) total_passed++;
        } else {
            if (run_test(&tests[i])) total_passed++;
        }
    }

    printf("Summary: %d / %zu tests passed.\n", total_passed, test_count);
    return (total_passed == (int) test_count) ? 0 : 1;
}

/* End of exercise-03-ed25519-verify.c. */
