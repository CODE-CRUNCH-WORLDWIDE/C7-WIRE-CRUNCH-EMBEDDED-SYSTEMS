# Exercise Solutions — Week 10

This document walks each exercise's reference solution and lists the canonical bugs students hit. Read it after you have attempted each exercise — not before.

---

## Exercise 1: UF2 Parser

### What the exercise asks

Read a UF2 file from disk, parse each 512-byte block, validate the magic numbers and family ID, and print a summary of which flash addresses the file would write to.

### Reference solution

The full source is in `exercise-01-uf2-parser.c`. The key function is `cc_uf2_parse_block`:

```c
uf2_result_t cc_uf2_parse_block(const uint8_t *block, uf2_block_payload_t *out) {
    if (block == NULL || out == NULL) {
        return UF2_ERR_BAD_BLOCK_NUMBER;
    }

    uint32_t magic_start0 = cc_read_le32(block + 0);
    uint32_t magic_start1 = cc_read_le32(block + 4);
    uint32_t flags        = cc_read_le32(block + 8);
    uint32_t target_addr  = cc_read_le32(block + 12);
    uint32_t payload_size = cc_read_le32(block + 16);
    uint32_t block_no     = cc_read_le32(block + 20);
    uint32_t num_blocks   = cc_read_le32(block + 24);
    uint32_t family_id    = cc_read_le32(block + 28);
    uint32_t magic_end    = cc_read_le32(block + 508);

    if (magic_start0 != UF2_MAGIC_START0_VALUE) return UF2_ERR_BAD_MAGIC0;
    if (magic_start1 != UF2_MAGIC_START1_VALUE) return UF2_ERR_BAD_MAGIC1;
    if (magic_end    != UF2_MAGIC_END_VALUE)    return UF2_ERR_BAD_MAGIC_END;
    if (payload_size > UF2_MAX_PAYLOAD)         return UF2_ERR_PAYLOAD_TOO_BIG;
    if ((flags & UF2_FLAG_FAMILYID_PRESENT) == 0u) return UF2_ERR_NO_FAMILY;
    if (family_id != UF2_FAMILY_RP2040)         return UF2_ERR_WRONG_FAMILY;
    if (block_no >= num_blocks)                 return UF2_ERR_BAD_BLOCK_NUMBER;

    out->target_addr = target_addr;
    out->length      = payload_size;
    out->data        = block + UF2_HEADER_SIZE;
    return UF2_OK;
}
```

The main loop reads the file, calls the parser block by block, and prints a one-line summary per block. Aggregate stats (lowest/highest target, overhead percentage) come at the end.

### Expected output

Run against a known UF2 file (the Pico SDK's `blink.uf2` makes a good test):

```text
$ ./uf2parse blink.uf2
UF2 file: blink.uf2
  size:   52224 bytes
  blocks: 102

  block   0/101  target=0x10000000  len= 256  family=0xe48bff56
  block   1/101  target=0x10000100  len= 256  family=0xe48bff56
  block   2/101  target=0x10000200  len= 256  family=0xe48bff56
  ...
  block 101/101  target=0x10006500  len= 256  family=0xe48bff56

summary:
  lowest target addr:  0x10000000
  highest target addr: 0x10006500
  total payload bytes: 26112
  overhead:            50.0%
```

The 50% overhead is fixed (256 payload bytes out of 512 block bytes); the absolute address range (`0x10000000`–`0x10006500`) shows the UF2 writes the second-stage bootloader at offset 0 and the application at 256.

### The canonical bugs

1. **Native-endian loads on a big-endian host.** This rarely matters for x86 or ARM but bites on PowerPC and SPARC. Always use explicit byte-by-byte reads (the `cc_read_le32` helper) for on-disk format fields.
2. **Strict block-number sequencing missing.** The spec does not require blocks to be in order in the file, but RP2040 UF2 files always are; missing the check lets a malformed file slip through.
3. **Skipping the family-ID check.** A UF2 file built for a different chip (Atmel SAMD21, family `0x68ed2b88`) will write to flash addresses that may or may not exist on the RP2040. Without the family check, you happily forward arbitrary addresses to `flash_range_program` and corrupt random flash.
4. **Forgetting to validate `payloadSize`.** A malformed block with `payloadSize=10000` would, if you trust it, read 10000 bytes from the 476-byte data region — a buffer overread by 9524 bytes.
5. **Allocating a per-file buffer instead of streaming.** A 1 MB UF2 fits in a 2 MB Pico's flash but not in its 264 KB of SRAM. The reference parser loads the entire file into a malloc'd buffer because it runs on the host; the on-device version would stream one block at a time.

---

## Exercise 2: Flash Write Primitives

### What the exercise asks

On the Pico, erase one 4 KB sector at flash offset `0x000FF000` and program it with a test pattern. Verify the contents by reading back through XIP. Measure the throughput.

### Reference solution

The source is in `exercise-02-flash-write-primitives.c`. The key is `do_flash_write`, marked `__not_in_flash_func` so the function body resides in SRAM:

```c
static void __not_in_flash_func(do_flash_write)(uint32_t flash_offset,
                                                const uint8_t *src,
                                                size_t length) {
    uint32_t saved = save_and_disable_interrupts();

    g_api.connect_internal_flash();
    g_api.flash_exit_xip();
    g_api.flash_range_erase(flash_offset, length, CC_FLASH_SECTOR_SIZE,
                            CC_FLASH_ERASE_CMD);
    g_api.flash_range_program(flash_offset, src, length);
    g_api.flash_flush_cache();
    g_api.flash_enter_cmd_xip();

    restore_interrupts(saved);
}
```

The function-pointer cache (`g_api`) is populated at startup; the erase opcode `0x20` and block size `4096` are the W25Q16JV's sector-erase command and granularity (Winbond §7.2.18, p. 37).

### Expected output

```text
=== Exercise 2: Flash write primitives ===
Target flash offset: 0x000ff000
Target memory addr:  0x100ff000
Length:              4096 bytes (one 4 KB sector)

Boot ROM function pointers cached.
Test pattern generated: pattern[0]=0x00 pattern[255]=0xff pattern[256]=0x11 pattern[4095]=0x55
Write complete in 51823 us (~45.00 ms erase + 6.40 ms program estimated).

VERIFY PASS: 4096 bytes match.
Effective throughput: 77.1 KB/s.
```

The 51.8 ms total is dominated by the 4 KB erase (~45 ms typical). The 16 page programs add another ~6.4 ms. The 77 KB/s throughput is the right order of magnitude for a single-sector benchmark; with batched writes that amortize the erase cost, sustained throughput reaches ~150 KB/s.

### The canonical bugs

1. **Forgetting `__not_in_flash_func`.** If the writer function lives in flash, the first time it tries to fetch its next instruction during the erase, XIP is disabled and the CPU faults. Symptom: HardFault on the first call.
2. **Forgetting to mask interrupts.** A UART RX interrupt fires during the erase, the ISR vector fetch returns garbage, HardFault. Always wrap the write in `save_and_disable_interrupts` / `restore_interrupts`.
3. **Passing a memory-mapped address instead of a flash offset.** `flash_range_erase(0x100FF000, ...)` rounds to `0x100FF000 / 4096 = 0x1010f0 * 4096`, which is way outside flash. The erase silently no-ops or wraps. Always use the offset (no `0x10000000` base).
4. **Length not a multiple of 4096 for erase, or 256 for program.** The Boot ROM rounds; you get more (or less) erased than you intended. Pre-align in the caller.
5. **Forgetting `flash_flush_cache`.** The XIP cache holds the pre-erase contents; reads through the XIP region show the old data even though flash holds the new data. The verify pass fails 50% of the time (depending on whether the cache lines happen to be evicted). Always flush after a write.

---

## Exercise 3: Ed25519 Verify

### What the exercise asks

Take a known-good Ed25519 signature (from RFC 8032 §7.1 test vector #1), verify it with the ed25519-donna library, and confirm the result. Then tamper with one bit of the signature and confirm the verifier rejects.

### Reference solution

The source is in `exercise-03-ed25519-verify.c`. The body is a sequence of `ed25519_sign_open` calls against hardcoded test vectors.

The donna library is in the mini-project's source tree. To build standalone (e.g., on your laptop for fast iteration):

```bash
cc -std=c11 -Wall -Wextra -O2 \
   -DED25519_REFHASH -DED25519_FORCE_32BIT -DED25519_NO_INLINE_ASM \
   -I ../../../mini-project/ed25519-donna/ \
   -o ed25519test exercise-03-ed25519-verify.c \
   ../../../mini-project/ed25519-donna/ed25519.c
```

### Expected output

```text
--- RFC 8032 §7.1 Test Vector #1 (empty message) ---
  public key (32 bytes): d75a980182b10ab7d54bfed3c964073a
    0ee172f3daa62325af021a68f707511a
  message: (empty)
  signature (64 bytes): e5564300c360ac729086e2cc806e828a
    84877f1eb8e5d974d873e06522490155
    5fb8821590a33bacc61e39701cf9b46b
    d25bf5f0595bbe24655141438e7a100b
  result: 0 (expected 0) -> PASS

--- RFC 8032 §7.1 Test Vector #2 (1-byte message 0x72) ---
  ...
  result: 0 (expected 0) -> PASS

--- Tampered signature (bit 0 of byte 32 flipped) ---
  result: -1 (expected: non-zero) -> PASS

Summary: 3 / 3 tests passed.
```

The verify time on the Pico (build the same source with the Pico SDK) is ~70 ms per call at the default 125 MHz clock. On a laptop, the same code runs in ~0.3 ms.

### The canonical bugs

1. **Wrong test vector bytes.** RFC 8032's hex is dense and easy to mis-transcribe by one nibble. Always copy-paste; never retype.
2. **Linking ed25519-donna without `-DED25519_REFHASH`.** The library compiles but expects an external SHA512 implementation. Symptom: linker error `undefined reference to sha512_compress`.
3. **Compiler optimization re-ordering across the verify call.** Ed25519's constant-time guarantees are not preserved across `-O3` in some toolchains. For verify-only this rarely matters (the secret data is on the host, not the device), but for sign operations it does. We use `-O2 -Os` to be safe.
4. **Calling `ed25519_sign_open` with `mlen=0` and `m` pointing to invalid memory.** Donna does not dereference `m` when `mlen=0` but a defensive C habit says always pass a valid pointer; pass `&buf[0]` even when `buf` is unused.
5. **Confusing the donna API with libsodium's.** Libsodium has `crypto_sign_verify_detached(sig, m, mlen, pk)` (note `sig` first). Donna has `ed25519_sign_open(m, mlen, pk, sig)` (signature last). Mixing them up returns success on a wrong signature because the bytes get interpreted as a public key.

---

## A general note on "I followed the spec and it doesn't work"

For all three exercises, "spec-compliant" code can still fail because of:

- **Cache coherency.** The XIP cache, the SIO peripheral, and any DMA-touched memory must be considered. `__DSB` and `__ISB` barriers exist for this.
- **Toolchain version drift.** The Pico SDK's headers move between releases; `rom_func_lookup` was once `rom_func_lookup_inline` and the spelling matters. Pin the SDK version in your CMake.
- **The chip's actual silicon revision.** RP2040 B2 is the current production silicon; earlier B0 and B1 dies had a documented quirk where `_flash_range_erase` returned before the chip's BUSY status cleared, leading to a `_flash_range_program` call against still-busy flash. The Boot ROM in B2 added a busy-wait. If your Pico is from 2021, you might be on B1 and the quirk affects you.

When in doubt, capture the Boot ROM function pointers' addresses at runtime and compare against the values listed in datasheet Table 167 (p. 134 for RP2040). If they differ, you have a non-stock Boot ROM (which exists only in some Raspberry Pi internal builds and almost certainly not on your Pico).
