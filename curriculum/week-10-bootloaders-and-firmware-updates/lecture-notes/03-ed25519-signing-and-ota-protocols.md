# Lecture 3 — Ed25519 Signing, the Signed Image Format, and OTA Protocols

> *Cryptographic signatures are a way of letting one party prove to another that a piece of data was authorized by the holder of a particular private key without revealing the private key itself. For firmware updates, this is the whole game. The device's bootloader holds the project's public key in flash. The build server holds the private key in a hardware security module or, in a hobbyist build, in a file in the developer's home directory. Every firmware image the build server emits carries a signature that the device verifies before booting. An attacker who has compromised the channel between build server and device — be it WiFi, a USB cable, a debug header — can replay any image they have ever observed, but they cannot forge a new one. This lecture is about that math, the file format we use to carry the signature, and the serial protocol the device uses to receive the file.*

## 1. Why Ed25519 and not RSA

For 25 years, "firmware signing" meant RSA-2048 with a SHA-256 hash. RSA is well-understood, every cryptographic library implements it, and verifying an RSA-2048 signature on a Cortex-M0+ takes about 80 ms with a hand-tuned implementation. The downside is that an RSA-2048 verifier in C is ~25 KB of code (the modular-exponentiation routine plus the SHA-256 plus the PKCS#1 padding plus the OID parsing) and the signature itself is 256 bytes.

Ed25519 is an elliptic-curve signature scheme published by Bernstein, Duif, Lange, Schwabe, and Yang in 2011 (paper at <https://ed25519.cr.yp.to/ed25519-20110926.pdf>) and standardized in RFC 8032 (2017). At the 128-bit security level, an Ed25519 signature is 64 bytes (32 bytes for the `R` component, 32 bytes for the `S` component), the public key is 32 bytes, and the verifier in C is ~12 KB of code. The verify operation on a Cortex-M0+ at 125 MHz takes about 70 ms (we will measure this in Exercise 3).

The two properties that matter for our bootloader:

1. **Size.** 12 KB of verifier code fits comfortably in our 32 KB bootloader budget; a 25 KB RSA verifier would not. Smaller is also faster, especially because Cortex-M0+ has no hardware multiplier wide enough to make RSA practical without a heroic amount of optimization.
2. **Determinism.** Ed25519 signatures are *deterministic* — the same private key signing the same message always produces the same signature. RSA-PSS (the modern RSA scheme) is randomized. Determinism makes signed firmware reproducible at the bit level, which is a feature when you are verifying the build server's output against a hash committed in git.

The mathematical foundation is the Edwards curve `edwards25519` defined over `GF(2^255 - 19)`. The curve equation is `-x^2 + y^2 = 1 + d*x^2*y^2` with `d = -121665/121666 mod p` (RFC 8032 §5.1, p. 13). The base point `B` has order `L = 2^252 + 27742317777372353535851937790883648493`. Signatures live in `Z_L × E[L]`. None of this matters for our bootloader; we treat the verifier as a black box: `verify(public_key, message, message_length, signature) → ok | fail`.

## 2. The verifier we use

`ed25519-donna` (Andrew Moon, public domain, <https://github.com/floodyberry/ed25519-donna>) is the reference C implementation we extract a verify-only subset from. The function we call is:

```c
int ed25519_sign_open(const unsigned char *m,
                      size_t mlen,
                      const ed25519_public_key pk,
                      const ed25519_signature RS);
```

`m` is a pointer to the message bytes. `mlen` is the message length. `pk` is the 32-byte public key. `RS` is the 64-byte signature. Returns `0` on success, non-zero on failure.

Internally, the function does:

1. Compute `H = SHA512(R || A || M)` where `R` is the first 32 bytes of `RS`, `A` is `pk`, `M` is the message.
2. Compute `k = H mod L`.
3. Compute `[8]R + [8]kA - [8]sB`, where `s` is the second 32 bytes of `RS` interpreted as a little-endian scalar.
4. Return `0` iff the resulting curve point is the identity.

The `[8]` cofactor multiplication is RFC 8032's "cofactored" verification. Some libraries use "cofactorless" which is equivalent for honestly-generated signatures but rejects some malleability. We use cofactored to match RFC 8032's specification and the majority of deployed implementations.

The verifier in our build is ~12 KB of `.text`. The compile-time flags:

- `-DED25519_REFHASH` — use the included SHA512 (we do not have a system one). Adds ~3 KB.
- `-DED25519_NO_INLINE_ASM` — Cortex-M0+ has no hardware multiplier-accelerator. Skip the assembly path.
- `-DED25519_FORCE_32BIT` — use 32-bit limbs. Cortex-M0+ has no 64-bit multiply.
- `-Os` for size.

The Pico SDK's `pico_set_program_size_pad()` is unhelpful here — it limits the *image* size but not the per-section size. We rely on the linker's `LENGTH = 32k` on the bootloader's FLASH region to catch oversize early.

## 3. The signed-image file format

Our `.ccf` format (Code Crunch Firmware, version 1) is:

```text
+--------+--------+----------------------------+
| Offset | Size   | Field                       |
+--------+--------+----------------------------+
|   0    |   4 B  | magic = 0x43434631u ('CCF1')|
|   4    |   4 B  | version (currently 1)       |
|   8    |   4 B  | fw_size_le32                |
|  12    |   4 B  | flags                       |
|  16    |  32 B  | fw_sha256                   |
|  48    | fw_size| firmware bytes              |
| 48+fw  |  64 B  | ed25519_signature           |
+--------+--------+----------------------------+
```

Total file size: `48 + fw_size + 64` bytes.

The header (offsets 0–47) and the firmware bytes (offsets 48 to `48 + fw_size - 1`) are what the signature covers. The signature itself (last 64 bytes) is not part of the signed range — chicken-and-egg.

The C struct mirrors the on-wire format:

```c
#define CCF_MAGIC   0x43434631u
#define CCF_VERSION 1u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t fw_size;
    uint32_t flags;
    uint8_t  fw_sha256[32];
    /* firmware bytes follow */
    /* 64 bytes of signature follow the firmware */
} ccf_header_t;
```

The `flags` field is reserved; bit 0 will indicate "encrypted body" in a future version, bit 1 "compressed body", etc.

The verifier reads the file end-to-end:

```c
int ccf_verify(const uint8_t *image,
               size_t image_size,
               const uint8_t public_key[32]) {
    if (image_size < (size_t)(48u + 64u)) return CCF_ERR_TOO_SHORT;

    const ccf_header_t *hdr = (const ccf_header_t *) image;
    if (hdr->magic   != CCF_MAGIC)   return CCF_ERR_BAD_MAGIC;
    if (hdr->version != CCF_VERSION) return CCF_ERR_BAD_VERSION;
    if ((size_t)(48u + hdr->fw_size + 64u) != image_size) {
        return CCF_ERR_BAD_SIZE;
    }

    /* Verify SHA256 of the header (excluding fw_sha256 itself) plus firmware. */
    uint8_t computed_sha[32];
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, image, 16u);                 /* magic..flags */
    sha256_update(&ctx, image + 48u, hdr->fw_size);  /* firmware */
    sha256_finalize(&ctx, computed_sha);

    if (constant_time_memcmp(computed_sha, hdr->fw_sha256, 32u) != 0) {
        return CCF_ERR_SHA_MISMATCH;
    }

    /* Verify Ed25519 over the header + firmware (i.e., the entire image
       minus the trailing 64-byte signature). */
    const uint8_t *signed_range_begin = image;
    size_t         signed_range_size  = image_size - 64u;
    const uint8_t *signature          = image + signed_range_size;

    int rv = ed25519_sign_open(signed_range_begin,
                               signed_range_size,
                               public_key,
                               signature);
    if (rv != 0) return CCF_ERR_BAD_SIGNATURE;

    return CCF_OK;
}
```

Two design choices to flag:

- **SHA256 over the body is redundant** if we also Ed25519-sign the same bytes. The Ed25519 signature is over `SHA512(R || A || M)` internally; the body integrity is already guaranteed. We include the explicit SHA256 in the header because (a) it lets the host transmission protocol validate chunks against a running SHA before the signature is even computed, and (b) it gives a fast pre-flight reject during the OTA receive phase: if a chunk's SHA fragment is wrong, the device rejects before doing the expensive Ed25519 work. The cost is 32 bytes per image, which is negligible.
- **The signature covers the header**. This means the version, the size, the SHA256, and the flags are all signed; an attacker who flips a flag to "encrypted" must also forge the signature. This is the right default; the alternative (signing only the firmware bytes) allows trivial tampering with the metadata.

## 4. The host-side signing tool

`cc-sign.py` (in `mini-project/`) is the Python script that takes a `.bin` file and produces a `.ccf`. Its actions:

1. Read the firmware bytes from the input file.
2. Compute the SHA256.
3. Construct the header (magic, version, size, flags=0, sha256).
4. Read the Ed25519 private key from `~/.cc-flash-keys/ed25519_private.pem`.
5. Sign the header + firmware bytes (i.e., the file-to-be minus the trailing signature).
6. Write `header || firmware || signature` to the output file.

The cryptography library does all the heavy lifting:

```python
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization
import hashlib, struct

def sign_image(fw_bytes: bytes, private_key_path: str) -> bytes:
    sha = hashlib.sha256(fw_bytes).digest()
    header = struct.pack('<IIII32s',
                         0x43434631,        # magic
                         1,                 # version
                         len(fw_bytes),     # fw_size
                         0,                 # flags
                         sha)               # fw_sha256
    with open(private_key_path, 'rb') as fh:
        key = serialization.load_pem_private_key(fh.read(), password=None)
    signed_range = header + fw_bytes
    signature = key.sign(signed_range)
    return signed_range + signature
```

The key is generated once with OpenSSL:

```bash
openssl genpkey -algorithm Ed25519 -out ~/.cc-flash-keys/ed25519_private.pem
openssl pkey -in ~/.cc-flash-keys/ed25519_private.pem \
             -pubout -outform DER \
       | tail -c 32 > ~/.cc-flash-keys/ed25519_public.bin
```

The `tail -c 32` is because OpenSSL's PEM-encoded public key wraps the raw 32-byte point in ASN.1 / SubjectPublicKeyInfo. The 32 bytes we want are the trailing octets.

The 32-byte public key is then `xxd -i` 'd into a C header and committed into the bootloader's source tree:

```c
/* Generated by `xxd -i ed25519_public.bin > public_key.h`. */
const uint8_t bootloader_public_key[32] = {
    0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18,
    /* ... 32 bytes total ... */
};
```

This file is in the repo. Anyone with read access to the public key can verify signatures (which is the whole point). Only someone with access to `~/.cc-flash-keys/ed25519_private.pem` can produce them.

## 5. The OTA protocol

The device-side OTA receiver runs in the *application*, not the bootloader. The reasons:

- TinyUSB and its CDC class are ~30 KB of code. They do not fit in the bootloader's 32 KB budget alongside Ed25519.
- The bootloader is reached only at reset; receiving a multi-second flash dump while booted would be jarring.
- The application has a stable USB identity (vendor/product ID) that hosts remember; the bootloader has no need to enumerate.

The protocol runs over the CDC virtual serial port the application already has from Week 9's mini-project. It is plaintext line-oriented, hex-encoded for binary payloads.

### Commands and responses

```text
Host: BEGIN <total_size_decimal> <sha256_hex>\n
Dev:  OK\n
   (or: ERR <code> <msg>\n)

Host: CHUNK <offset_decimal> <length_decimal> <hex_bytes>\n
Dev:  OK <offset>\n
   (or: ERR <code> <msg>\n)

Host: (repeats CHUNK until offset == total_size)

Host: END\n
Dev:  OK\n     (after verifying overall SHA256 against announced)
   (or: ERR <code> <msg>\n)

Host: REBOOT\n
Dev:  OK\n     (then triggers reset)
```

Each line ends with `\n` (LF only, not CR-LF). All numbers are decimal ASCII. The hex bytes are lowercase pairs without separators. A 256-byte chunk produces a 512-character hex string; the full command line is `~530 characters` including the `CHUNK ` prefix, the offset (~6 chars), the length (`256`), and the trailing `\n`. CDC ACM's bulk-OUT endpoint accepts up to 64 bytes per packet, so a chunk transfers as ~9 packets back-to-back. The host should not pipeline chunks — wait for the `OK <offset>` response before sending the next.

### The device state machine

```c
typedef enum {
    OTA_IDLE,           /* No transfer in progress */
    OTA_RECEIVING,      /* BEGIN received; chunks coming in */
    OTA_FINALIZED,      /* END received; SHA verified; awaiting REBOOT */
    OTA_ERROR,          /* An error occurred; ignore further chunks */
} ota_state_t;

typedef struct {
    ota_state_t  state;
    uint32_t     total_size;
    uint8_t      expected_sha256[32];
    uint32_t     bytes_received;
    sha256_ctx_t running_sha;
    uint32_t     last_offset;
} ota_session_t;
```

On `BEGIN`, the device:

1. Validates `total_size` is in range `[48 + 64, MAX_STAGING_SIZE]`.
2. Parses the 64-character SHA256 hex into 32 bytes.
3. Erases the entire staging bank (this takes `~752 KB / 4 KB × 45 ms = ~8.5 seconds`).
4. Initializes the running SHA context.
5. Sets state to `OTA_RECEIVING` and `bytes_received = 0, last_offset = 0`.
6. Responds `OK`.

On each `CHUNK`:

1. Validates `offset == bytes_received` (chunks must be in order).
2. Validates `length <= 256` and `length > 0`.
3. Parses the hex into a `uint8_t buffer[256]`.
4. Calls `flash_safe_write(staging_flash_offset + offset, buffer, ROUND_UP(length, 256))`. The round-up to 256 is necessary because the flash programmer requires a page-aligned length; we pre-pad with `0xFF` if the chunk is short.
5. Updates the running SHA with `length` bytes.
6. Updates `bytes_received += length` and `last_offset = offset`.
7. Responds `OK <offset>`.

On `END`:

1. Validates `bytes_received == total_size`.
2. Finalizes the running SHA and compares to `expected_sha256`.
3. If match: writes the metadata page setting `state = BL_STATE_SWAP_REQUESTED`, `staging_size = total_size`, and `staging_sha256 = expected_sha256`. (Note: we do not have the signature yet at this point — the signature is part of the firmware bytes, which we just wrote to flash; the bootloader will read the signature from the staging bank's footer and verify it on next boot.)
4. If mismatch: erases the staging bank, sets state to `OTA_ERROR`.
5. Responds `OK` or `ERR`.

On `REBOOT`:

1. Sets watchdog scratch4 = `0xCCC10A55u` (the "swap requested" magic).
2. Configures the watchdog with a 100 ms timeout.
3. Disables interrupts and spins.
4. Responds `OK` *before* the spin so the host sees the acknowledgment.

The watchdog triggers a reset; the Boot ROM runs; the bootloader sees scratch4 = `0xCCC10A55u`, performs the swap, and boots the new application.

### Why hex and not binary

Binary would halve the bandwidth (256 bytes per chunk instead of 512 chars), but introduces problems:

- Embedded developers cannot type `\xCC\xC1\x0A\x55` into `screen /dev/cu.usbmodem...` to test the protocol by hand. Hex is debuggable from a terminal.
- A binary protocol over CDC must handle line-buffering quirks. macOS's tty driver does line-discipline by default; switching to raw mode is required and forgotten by half of users.
- The bandwidth difference doesn't matter for our 752 KB transfer at 5 KB/s — both take in the same order of seconds.

For a production deployment with multi-megabyte firmware, switching to a binary framing (a 4-byte length prefix per chunk) is the right move. We use hex for week 10 to keep debugging trivial.

## 6. The host-side `cc-flash` tool

`cc-flash.py` reads a `.ccf` file and uploads it. The skeleton:

```python
import serial, hashlib, time, sys

def upload(ccf_path: str, port: str, baudrate: int = 115200):
    with open(ccf_path, 'rb') as fh:
        image = fh.read()

    total = len(image)
    sha = hashlib.sha256(image).hexdigest()

    ser = serial.Serial(port, baudrate, timeout=15.0)

    ser.write(f'BEGIN {total} {sha}\n'.encode())
    resp = ser.readline().decode().strip()
    if resp != 'OK':
        sys.exit(f'BEGIN rejected: {resp}')

    offset = 0
    while offset < total:
        chunk = image[offset : offset + 256]
        hex_str = chunk.hex()
        ser.write(f'CHUNK {offset} {len(chunk)} {hex_str}\n'.encode())
        resp = ser.readline().decode().strip()
        if not resp.startswith('OK'):
            sys.exit(f'CHUNK {offset} rejected: {resp}')
        offset += len(chunk)
        if offset % 4096 == 0:
            print(f'  uploaded {offset} / {total} bytes ({100*offset/total:.1f}%)')

    ser.write(b'END\n')
    resp = ser.readline().decode().strip()
    if resp != 'OK':
        sys.exit(f'END rejected: {resp}')

    ser.write(b'REBOOT\n')
    resp = ser.readline().decode().strip()
    if resp != 'OK':
        sys.exit(f'REBOOT rejected: {resp}')

    print('Upload complete. Device rebooting.')
```

A full version with progress bar, retry logic, and `argparse` is ~200 lines. It is `mini-project/cc-flash.py` in the repo.

## 7. The anti-bricking pattern

The OTA flow gets the new firmware into staging and asks the bootloader to swap on next boot. The bootloader verifies the signature, copies staging → active, reboots, and the new application runs. **But what if the new application is broken?** The signature was correct (the build server signed garbage but legitimately), the bootloader has no way to know.

Mitigation: the bootloader, on a swap-boot, sets `state = BL_STATE_BOOTING_NEW` and `boot_confirmed = 0` in metadata. The new application, after initializing successfully, must:

1. Open the metadata page.
2. Set `boot_confirmed = 1`.
3. Write the metadata back with the ping-pong protocol.

If the new application crashes before step 3, the next reset finds `state = BL_STATE_BOOTING_NEW` and `boot_confirmed = 0`. The bootloader sees this and increments `boot_attempts`. If `boot_attempts >= 3`, the bootloader sets `state = BL_STATE_ROLLBACK_PENDING` and falls back to the *previous* firmware — which is still in the staging bank, because the swap was a copy and the bootloader keeps the source intact until the new application confirms.

Implementing this requires the bootloader to remember the previous-good firmware. We do this by **not** erasing staging after a swap. Staging holds the new image during transfer; after the swap-copy, staging holds the *outgoing* image (the previous-known-good). The next OTA upload overwrites it. This wastes one bank but gives us a free rollback.

The application's confirmation code:

```c
#include "bootloader_common.h"

static bool confirm_boot(void) {
    /* Initialize whatever needs initializing first — peripherals, USB,
       the OTA receiver. Then declare success. */
    if (!system_smoke_test_passed()) {
        return false;
    }

    bootloader_metadata_t meta;
    if (metadata_read(&meta) != BL_OK) return false;

    if (meta.state == BL_STATE_BOOTING_NEW) {
        meta.boot_confirmed = 1u;
        meta.boot_attempts  = 0u;
        meta.state          = BL_STATE_IDLE;
        if (metadata_write(&meta) != BL_OK) return false;
    }

    return true;
}

int main(void) {
    system_init();

    if (!confirm_boot()) {
        watchdog_reboot(0, 0, 0);  /* Bootloader will roll back. */
    }

    /* Normal application loop. */
    application_main();
    return 0;
}
```

`system_smoke_test_passed()` is application-specific: it might check that USB enumerates within 5 seconds, that the WiFi radio responds, that the BME280 sensor is on the I²C bus, that any critical peripheral is functional. Anything below "the application is usable" should fail the smoke test and trigger rollback.

### Rollback in the bootloader

When the bootloader detects `boot_attempts >= 3` on a `BL_STATE_BOOTING_NEW` page, it:

1. Reads the staging bank (which holds the previous-good firmware).
2. Verifies its signature.
3. If valid, copies staging → active, sets `state = BL_STATE_IDLE`, `boot_confirmed = 1` (this image is known good — it was running before the failed update), `boot_attempts = 0`.
4. Reboots into the rolled-back firmware.

The user-visible effect: a bad update results in the device booting the old firmware after 3 failed attempts (which is 3 × the watchdog timeout, typically 30–90 seconds). The device is not bricked. The user can attempt the OTA again with a corrected image.

## 8. Threat model — what this scheme protects against

Our signed-image bootloader protects against:

- **Tampering with firmware in transit.** An attacker on the USB cable, the WiFi link, or any future BLE/LoRa channel cannot replace the image with a different one because they cannot forge a valid Ed25519 signature.
- **Unsigned binaries.** An attacker who runs `cc-flash` against the device with a self-built `.ccf` cannot get the bootloader to boot it because the bootloader rejects the signature.
- **Downgrade attacks (partially).** If we add a monotonic version counter in metadata (a feature we leave for Week 17 with OTP), the bootloader can refuse to boot images with a version lower than the current. Without OTP, the version counter is mutable, so downgrade is possible by overwriting metadata directly via SWD.

It does **not** protect against:

- **Attackers with physical access to SWD.** SWD can read and write flash directly, bypassing the bootloader. The countermeasure is the SWD-lock mechanism in RP2040 datasheet §2.7 ("Debug", pp. 116–129), which requires writing a fuse to disable SWD permanently. We do not touch this in Week 10.
- **Attackers with physical access to the chip pads.** Decapping and reading the flash chip directly is feasible at ~$10k of lab equipment. Encrypted images mitigate this; we do not implement encryption in Week 10.
- **Side-channel attacks on the verifier.** Power and timing analysis of the Ed25519 verifier could in principle leak the public key (which is not secret) but not the private key (which is not on the device). Ed25519 is designed to be constant-time and our verifier inherits that property.
- **Attackers who steal the build server's private key.** This is the catastrophic threat; if it happens, the only mitigation is to rotate the key (which requires re-flashing every device with a new bootloader that has the new public key embedded — a logistical disaster). The countermeasure is to store the private key in an HSM (hardware security module) so it cannot be exfiltrated.

In short: our scheme makes casual tampering hard and makes professional tampering require lab equipment or insider compromise. That is the appropriate level for a hobbyist or university project.

## 9. Summary

Ed25519 is a 64-byte signature over `SHA512(R || A || M)` with a 32-byte public key. Our `.ccf` file format is a 48-byte header plus the firmware plus a 64-byte trailing signature. The host signs with OpenSSL-generated keys; the device verifies with ed25519-donna (verify-only, 12 KB of code). The OTA protocol is plaintext line-oriented hex over CDC; ~150 seconds to upload 752 KB; the device writes to staging during the transfer, marks swap-requested in metadata, and reboots. The bootloader does the swap on next boot. The anti-bricking pattern is a watchdog-confirmation flag the new firmware must set after its smoke tests pass; if it doesn't, the bootloader rolls back after 3 attempts.

Tomorrow: the mini-project. You implement bootloader.c (1500 LOC), application.c (800 LOC), and cc-flash.py (200 LOC) and demonstrate an end-to-end OTA. Allocate 6 hours.

## References for this lecture

- RFC 8032, "Edwards-Curve Digital Signature Algorithm (EdDSA)", Josefsson & Liusvaara, January 2017. <https://www.rfc-editor.org/rfc/rfc8032.html> — §5.1 (the Ed25519 algorithm), §7.1 (test vectors).
- ed25519-donna, Andrew Moon, public domain. <https://github.com/floodyberry/ed25519-donna>
- Bernstein et al., "High-speed high-security signatures", 2011. <https://ed25519.cr.yp.to/ed25519-20110926.pdf>
- MCUboot design doc, "Image swap with rollback" section. <https://docs.mcuboot.com/design.html#image-swap-with-rollback>
- MCUboot serial-recovery protocol. <https://docs.mcuboot.com/serial_recovery.html>
- USB DFU 1.1 §6 "DFU Operation", pp. 16–25. <https://www.usb.org/sites/default/files/DFU_1.1.pdf>
- RP2040 datasheet §2.7 "Debug", pp. 116–129; §2.8 "Bootrom", pp. 130–141; §4.7 "Watchdog", pp. 559–570.
