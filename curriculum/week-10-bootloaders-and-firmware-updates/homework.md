# Homework — Week 10

Six problems. Estimated total time: ~6 hours over the week. Submit answers as commits to your fork of the course repo; each problem has a designated file path under `homework/week-10/`.

---

## Problem 1 — Walk the boot2 assembly (1 hour)

Open `pico-sdk/src/rp2_common/boot_stage2/boot2_w25q080.S`. Read it line by line. Produce a `homework/week-10/boot2-walk.md` that:

1. Annotates each of the ~200 lines of assembly with a one-line comment in plain English.
2. Identifies the four phases: clock setup → SSI register configuration → QSPI enable-quad-mode sequence → jump-to-flash[256].
3. Cites the relevant RP2040 datasheet section for each register write (§4.10.13.1 "SSI register block", §4.10.14 "Functions", pp. 631–640).
4. Notes the position-independence pattern: every constant is loaded via `ldr Rx, =value` (which the assembler resolves to a PC-relative literal pool load).

The deliverable is a markdown document of ~600 lines, with the assembly inlined and your annotations interspersed.

---

## Problem 2 — Write a UF2 packer (1 hour)

Write `homework/week-10/uf2pack.py` — a Python 3 script that:

1. Reads a `.bin` file from argv[1] (a flat firmware binary, no UF2 framing).
2. Reads a starting flash address from argv[2] as a hex string (e.g., `0x10000000`).
3. Produces a `.uf2` file at argv[3] with one block per 256-byte chunk, correctly populating `magicStart0`, `magicStart1`, `flags = 0x2000`, `targetAddr`, `payloadSize`, `blockNo`, `numBlocks`, `familyID = 0xE48BFF56`, the 476-byte payload (256 valid bytes + 220 zero pad), and `magicEnd`.
4. Verifies the output by reading it back with your Exercise 1 parser and confirming the parser accepts every block.

Use `struct.pack('<8I', ...)` for the header. Use `b'\x00' * 220` for the payload pad. The full script should be ~80 lines.

Test: convert the `blink.bin` from your SDK examples to a UF2, drag it onto the BOOTSEL drive, confirm the Pico blinks.

---

## Problem 3 — Read and summarize the USB DFU 1.1 state machine (1 hour)

Open the USB DFU 1.1 spec at <https://www.usb.org/sites/default/files/DFU_1.1.pdf>. Read §6 (pp. 16–25) carefully.

Write `homework/week-10/dfu-state-machine.md` that:

1. Lists all 10 states with their numeric values (per §6.1, p. 16).
2. For each transition (there are 23), gives the source state, the trigger (host request or device action), and the destination state.
3. Identifies which states are unreachable for a download-only device (no `DFU_UPLOAD`).
4. Identifies which states are unreachable for a manifest-tolerant device (i.e., `bitManifestationTolerant = 1` in the functional descriptor).
5. Includes a transition table as a markdown table.

The deliverable is ~300 lines of markdown. Cite §6 throughout.

---

## Problem 4 — Compute the worst-case OTA upload time (30 minutes)

Given:

- Pico's full-speed USB CDC bulk endpoint: ~12 Mbit/s on the wire, ~1.0 MB/s sustained user-data throughput on macOS (measured in Week 9).
- Our protocol's hex encoding: 2 chars per byte.
- Each `CHUNK` line is ~10 bytes of framing (`CHUNK ` + offset + length + spaces + `\n`) plus 512 bytes of hex.
- The device's flash-write latency per 256-byte chunk is ~3 ms (one page program) plus ~0 ms (the 4 KB erase was already done upfront).
- The staging-bank erase is ~8.5 seconds typical (188 sectors × 45 ms).
- The Ed25519 verify on the new image is ~70 ms.

Compute the end-to-end upload time for a 600 KB firmware image. Show your work in `homework/week-10/ota-timing.md`. Break down the time into: BEGIN-to-OK latency (erase), per-chunk RTT (host TX + device write + device ACK), END-to-OK latency, REBOOT to bootloader entry, swap copy (752 KB at ~5 MB/s flash write through), Ed25519 verify, application boot.

Compare to a hypothetical binary-framed protocol that halves the per-chunk bytes-on-wire. By how much would the total improve?

---

## Problem 5 — Design a metadata layout for a multi-key bootloader (1.5 hours)

The current bootloader has one public key embedded. Suppose you want to support **N public keys** so that you can rotate signing keys without re-flashing the bootloader.

Design (do not implement) a metadata layout that:

1. Stores up to 4 public keys in metadata pages.
2. Each key has a "valid" flag and a "version" (so older keys can be retired).
3. The bootloader, when verifying an image, tries each valid key in order until one succeeds or all fail.
4. Adding a new key is a metadata-write operation; revoking an old key is also a metadata-write.

Document the design in `homework/week-10/multi-key-design.md`. Required sections:

1. **Threat model** — what does multi-key buy you?
2. **Layout** — the C struct for the new metadata, with field sizes.
3. **Atomicity** — how do you add or revoke a key without leaving the metadata in an inconsistent state? (Hint: ping-pong with a sequence number, same as the simple bootloader.)
4. **Migration** — if a device shipped with the simple metadata layout, how do you transition it to the multi-key layout without re-flashing the bootloader? (Hint: leave a slot in the old struct that holds "metadata version", and the bootloader interprets a higher version differently.)
5. **Key storage size budget** — 4 keys × 32 bytes = 128 bytes; plus 4 valid-flags + 4 versions = 32 more bytes. Trivial compared to the 4 KB sector size.

The deliverable is a 1500-word design doc.

---

## Problem 6 — Implement boot2 for a hypothetical chip (1 hour)

The Adesto AT25SF128A is a 128-Mbit SPI flash chip used on some non-Pico RP2040 boards. Its command set differs slightly from the W25Q16JV:

- Quad-mode-enable command: `0x35` (read status register 2), check bit 1 to determine if QE is set; if not, send `0x31` (write status register 2) with bit 1 set.
- Quad I/O Fast Read: also `0xEB`, same as Winbond. Convenient.
- Sector erase: also `0x20`. Convenient.

Open `pico-sdk/src/rp2_common/boot_stage2/boot2_w25q080.S`. Adapt it for the AT25SF128A. The key changes are:

1. The QE-enable sequence (the W25Q080's variant uses `0x06` `WREN` then `0x31` write-SR2; the AT25SF128A also uses `0x06` then `0x31` — almost identical, but you must verify against the AT25SF128A datasheet §6.1.2 "Write Status Register Byte 2 (31h)", p. 12).
2. Some boards use only the standard Read instruction `0x03` instead of the Quad I/O Fast Read; check your board's datasheet.

Deliver `homework/week-10/boot2_at25sf128a.S` — the modified assembly. Annotate every change relative to `boot2_w25q080.S` with a comment.

Verify the resulting boot2 is exactly 252 bytes after assembly (use `arm-none-eabi-objdump -h boot2.o` to check the size of the `.boot2` section). The 4-byte CRC32 trailer is added by the SDK's `pad_checksum` script; you do not write that.

---

## Submission

Commit all six problems to your fork under `homework/week-10/`. Tag the commit `week-10-homework`.

```bash
git add homework/week-10/
git commit -m "week 10 homework: boot2 walk, uf2 packer, dfu state machine, ota timing, multi-key design, boot2_at25sf128a"
git tag week-10-homework
git push origin main --tags
```

The teaching team reviews homework asynchronously; expect feedback within ~5 business days.

---

## References for the homework set

- RP2040 datasheet §2.8 (Bootrom), §4.10 (QSPI), §2.8.5 (USB MSC).
- Microsoft UF2 spec, <https://github.com/microsoft/uf2>.
- USB DFU 1.1, <https://www.usb.org/sites/default/files/DFU_1.1.pdf>.
- Winbond W25Q16JV datasheet.
- Adesto AT25SF128A datasheet, <https://www.dialog-semiconductor.com/sites/default/files/2020-09/AT25SF128A-DS.pdf>.
- MCUboot design doc, "Encrypted images" and "Multiple signing keys" sections.
