# Quiz — Week 10

Ten questions. Closed-book on the spec citations (you should be able to recite the section number and the page); open-book on the C syntax. ~45 minutes. Answer key is at the bottom; do not peek before completing.

---

## Question 1

The RP2040 Boot ROM, on cold reset, performs a CRC32 check before deciding whether to jump to flash. What region does it CRC, and what value does it compare against?

(A) Flash[0..255] against the CRC32 stored at flash[252..255], MPEG-2 polynomial, no final XOR.
(B) Flash[0..511] against the CRC32 stored at flash[508..511], IEEE polynomial, final XOR with 0xFFFFFFFF.
(C) Flash[256..511] against the value of `WATCHDOG_SCRATCH4`.
(D) The Boot ROM does not CRC anything; it always jumps to flash[0].

## Question 2

You hold BOOTSEL and power-cycle the Pico. The chip enumerates as `RPI-RP2`. You drop a `.bin` file (not a UF2) onto the drive. What happens?

(A) The bytes are written to flash starting at offset 0.
(B) The bytes are written to flash starting at offset 256.
(C) The file is silently rejected; flash is unchanged.
(D) The drive disconnects and the chip reboots into the previous firmware.

## Question 3

A UF2 file's first block has `flags = 0x00002000`, `targetAddr = 0x10000100`, `payloadSize = 256`, `blockNo = 0`, `numBlocks = 10`, and `familyID = 0xE48BFF56`. Are these values consistent for an RP2040 program? Justify each field.

## Question 4

In the dual-bank layout described in Lecture 2, what is the byte at memory address `0x10008100` when the application bank is empty (never written)?

(A) `0x00`.
(B) `0xFF`.
(C) Undefined; the address is in unmapped space.
(D) The byte is the first byte of the bootloader's text segment.

## Question 5

The bootloader's `boot_application` function performs three actions before jumping to the application's reset handler. List them in order and explain why each is necessary.

## Question 6

The metadata page's `crc32` field covers all preceding fields of the `bootloader_metadata_t` struct. Why is this CRC necessary given that the bootloader could simply use the `magic` field as a validity marker?

(A) The CRC catches partial writes from power loss mid-program.
(B) The CRC is required by FAT12.
(C) The CRC protects against bit-rot in flash over years of storage.
(D) Both (A) and (C).

## Question 7

The Ed25519 public key in our bootloader is 32 bytes long. The corresponding private key is 32 bytes long (per RFC 8032 §5.1.5, p. 14). Where is each key stored in the project, and what is the consequence of a leak of each?

## Question 8

In the OTA protocol described in Lecture 3, the host sends a `BEGIN` command and the device responds with `OK` only after erasing the staging bank. What is the worst-case latency between `BEGIN` and `OK`, and what dominates it?

(A) ~50 µs; the USB control-transfer latency.
(B) ~8.5 seconds; the staging-bank erase (~752 KB / 4 KB sectors × 45 ms per sector).
(C) ~150 ms; the Ed25519 verify.
(D) ~1 ms; the line-parser overhead.

## Question 9

The watchdog-confirmation anti-bricking pattern requires the application to set `boot_confirmed = 1` in metadata within N seconds of boot. What goes wrong if `N` is too small (1 second), and what goes wrong if `N` is too large (60 seconds)?

## Question 10

The Boot ROM exposes flash functions through a lookup table at offset `0x14`. Write the C code that retrieves the `flash_range_program` function pointer (table code `('R', 'P')`) and prints its address. Assume `<pico/bootrom.h>` is available.

---

## Answers

### Q1: A

The Boot ROM CRCs flash[0..251] against the 4 bytes at flash[252..255]. The algorithm is CRC32-MPEG-2 (polynomial `0x04C11DB7`, initial value `0xFFFFFFFF`, no final XOR, MSB-first). RP2040 datasheet §2.8.1.2, p. 132 documents this.

### Q2: C

The Boot ROM's USB MSC firmware accepts only UF2 files. Raw `.bin` files written to the drive are silently rejected. The host writes appear in the FAT12 directory but the Boot ROM ignores any file that does not have valid UF2 block magic. RP2040 datasheet §2.8.5, p. 138.

### Q3: Yes, consistent.

- `flags = 0x00002000`: family ID present, no other flags. Correct for RP2040.
- `targetAddr = 0x10000100`: 256 bytes into flash, the standard application entry point.
- `payloadSize = 256`: the canonical RP2040 UF2 payload size; matches the flash page size.
- `blockNo = 0, numBlocks = 10`: first block of a 10-block file. The file is `10 × 512 = 5120 bytes`.
- `familyID = 0xE48BFF56`: the RP2040 family ID, registered in `microsoft/uf2/utils/uf2families.json`.

All fields cohere. The file would write 10 × 256 = 2560 bytes of program starting at `0x10000100`.

### Q4: B

Erased flash is all `0xFF` on the W25Q16JV (Winbond §6.1.4, p. 14). An empty application bank reads `0xFF` for every byte.

### Q5: Disable interrupts, relocate VTOR, set MSP and jump.

1. **Disable interrupts** (`__disable_irq()`). If an interrupt fires after we've set the MSP but before we've jumped, the ISR vector is fetched from the bootloader's still-resident vector table, but the bootloader's `.bss`/`.data` may have been clobbered by the application's reset handler running partway. Always mask before the handoff.
2. **Relocate VTOR** (`SCB->VTOR = application_vt_addr`). The Vector Table Offset Register tells the CPU where to look for interrupt vectors. Without this, any interrupt that fires in the application dispatches through the bootloader's now-stale vectors. ARMv6-M ARM §B3.2.5.
3. **Set MSP and jump** (`__set_MSP(*vt); ((void(*)())vt[1])();`). The Cortex-M0+ vector table's first entry is the initial SP and second entry is the reset handler. Setting MSP and calling the reset handler simulates the chip's own reset-time behavior.

### Q6: A

(B) is wrong; FAT12 has nothing to do with our metadata. (C) is true in theory but flash bit-rot is a multi-year phenomenon and not the proximate concern; the chip's data-retention spec (W25Q16JV §10.6, p. 67) is 20 years at 85 °C. (A) is the immediate motivation: a power loss between `flash_range_erase` and `flash_range_program` leaves the metadata partially erased (all `0xFF`), and the all-`0xFF` state has a valid `magic` value if you set magic to `0xFFFFFFFF` (which we deliberately do not). The CRC catches the partial-write state. Both (A) and (C) are correct; (D) is the right multiple-choice answer.

### Q7

The **public key** is in the bootloader's source tree, embedded as a `const uint8_t bootloader_public_key[32]` array in `mini-project/public_key.h`. It is part of the bootloader's `.rodata` section in flash. A leak is harmless: the public key is, by definition, public. Anyone with the public key can verify signatures but cannot produce them.

The **private key** is at `~/.cc-flash-keys/ed25519_private.pem` on the developer's laptop, generated by `openssl genpkey`. A leak of the private key is catastrophic: anyone with it can produce signatures the bootloader accepts, which lets them deploy arbitrary firmware to every device in the fleet. The mitigation is to store the private key in a hardware security module (HSM); for a hobbyist project, file-permission `0400` on the PEM file is the practical security.

### Q8: B

The staging bank is 770,304 bytes ≈ 752 KB. The W25Q16JV's typical sector-erase time is 45 ms (Winbond §10.2, p. 65); max is 400 ms. With 188 sectors to erase, the typical total is 188 × 45 ms ≈ 8.5 seconds; worst-case 188 × 400 ms ≈ 75 seconds.

(A) confuses the control-transfer latency with the operation latency. (C) is the verify time, which happens at boot, not during BEGIN. (D) is far too optimistic — line parsing alone is fast, but the erase dominates.

### Q9

**N too small (1 second).** The application has not yet completed initialization — WiFi has not associated, USB has not enumerated, the BME280 sensor on I²C has not responded — and the bootloader rolls back. The user sees ping-pong between the new and old firmware on every boot. The fix is to make N long enough to cover the application's longest reasonable startup path (often 5–10 seconds is right).

**N too large (60 seconds).** A genuinely broken firmware that loops forever doing nothing useful runs for a full minute before the watchdog fires and the bootloader rolls back. The user experience is "the device is bricked for a minute every boot until the rollback finally happens." More dangerously, if the firmware appears to work for the first 55 seconds and then crashes (a slow race condition with a timer interrupt, say), it might happen to set `boot_confirmed` just before the crash — the bootloader thinks the firmware is good, and the user faces a 55-second-then-crash cycle forever. The fix is to keep N tight enough that "appears to work" means "actually works": 10 seconds is typical for our class of device.

### Q10

```c
#include <stdio.h>
#include <inttypes.h>
#include "pico/bootrom.h"

int main(void) {
    /* rom_func_lookup returns the function address as a void *. */
    void *fn = rom_func_lookup(rom_table_code('R', 'P'));
    printf("flash_range_program is at 0x%08" PRIxPTR "\n",
           (uintptr_t) fn);
    return 0;
}
```

The exact address varies by RP2040 silicon revision; on the B2 die in the Pico, `flash_range_program` is typically at `0x00003ad8` (in the lower 16 KB of the Boot ROM mask area). Datasheet Table 167, p. 134, lists the addresses for reference but the lookup is the supported API; addresses can shift between revisions.

(End of quiz.)
