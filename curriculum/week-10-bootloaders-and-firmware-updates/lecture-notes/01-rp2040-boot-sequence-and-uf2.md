# Lecture 1 — The RP2040 Boot Sequence and the UF2 Format

> *Every CPU on Earth boots the same way at the lowest level. It reads four bytes from a fixed address that holds the initial program counter, four more for the initial stack pointer, sets those two registers, and starts executing. The differences are in the choreography around those eight bytes. The RP2040's choreography is three stages deep, the first stage is in ROM you cannot modify, and the dance ends at flash offset 256 of a chip that the silicon does not even know how to talk to until Stage 1 has finished configuring the QSPI controller. This lecture is about those three stages and the file format Microsoft invented to drop firmware onto the chip without writing a programmer.*

## 1. The chip at cold reset

When power rails come up clean and the `RUN` pin is high, the RP2040's two Cortex-M0+ cores leave reset. Core 1 is held by the SIO until core 0 explicitly releases it; we will not discuss core 1 boot at all this week because by the time core 1 starts, all the interesting bootloader machinery has already run on core 0 (RP2040 datasheet §2.8.2, p. 132).

Core 0's first action is to read `0x00000000` and `0x00000004` from the memory map. The address space at this point is **not** what it will be later. Specifically:

- `0x00000000` — `0x00003FFF` is mapped to the **Boot ROM** (16 KB of mask ROM). Datasheet §2.6.1, p. 24, "Address Map", confirms this.
- `0x00004000` — `0x0FFFFFFF` is reserved / unmapped.
- `0x10000000` — `0x1FFFFFFF` is the **XIP region** — the QSPI flash, accessible for read-only execution once the QSPI controller has been configured. At cold reset, **the XIP controller is not configured** — the flash returns garbage. This is the central fact that motivates Stage 1.
- `0x20000000` onwards is SRAM and peripherals, accessible immediately.

Because `0x00000000` is in the Boot ROM, the initial PC and SP at reset are loaded from the Boot ROM's vector table. The Boot ROM's reset handler runs first — there is no way to skip it.

Read the datasheet's Figure 11 on p. 131 before you continue. It is a single-page flowchart and it tells the entire story of this lecture. Memorize the three branch points: (1) BOOTSEL held? (2) CRC of flash[0..251] matches flash[252..255]? (3) USB attach within 1 second of failing into the recovery state?

## 2. Stage 0 — the Boot ROM

The Boot ROM is 16 KB of mask-programmed ROM baked into the silicon at fab time. Its source code is not published. Its behavior is fully specified in datasheet §2.8.1, pp. 130–132. The actions, in order:

1. **Clock setup.** The Boot ROM sets the system clock to the ring oscillator (ROSC) at approximately 6.5 MHz. The crystal oscillator (XOSC) and the PLLs are not yet configured. Any code that runs here must be tolerant of clock jitter and the low frequency.
2. **Watchdog and reset cause inspection.** The Boot ROM reads the `WATCHDOG_REASON` register (datasheet §4.7.6, p. 562) to determine whether this is a cold reset, a watchdog reset, or a programmed reboot. The watchdog can carry "scratch" values across reset (registers `WATCHDOG_SCRATCH0` through `WATCHDOG_SCRATCH7`); the Boot ROM uses some of these to honor `picotool reboot -u -f` and similar commands. This is the mechanism by which a running program can force the chip back into USB MSC mode without a hardware button press — `WATCHDOG_SCRATCH4` set to `0xB007C0D3u` ("BOOT CODE") plus a watchdog reset routes the Boot ROM into USB MSC mode on the next boot.
3. **BOOTSEL check.** GPIO 23 is the BOOTSEL line on the Pico board. The Boot ROM asserts the QSPI chip-select (which is shared with GPIO 23 — the resistor divider on the board lets BOOTSEL be both a pin and a CS sense at the same time, a deeply Raspberry-Pi-flavored bit of cost-engineering) and reads back the line. If the line is held low (button pressed to ground), the Boot ROM treats BOOTSEL as held and goes straight to step 5.
4. **Stage 1 CRC check.** The Boot ROM reads bytes 0–251 of flash (256 bytes total at offset `0x10000000`) into SRAM. It computes a CRC32-MPEG-2 over those 252 bytes and compares it against bytes 252–255 (the trailing 4-byte CRC). If they match, the Boot ROM jumps to flash offset 0 via `bx`. If they mismatch, the Boot ROM falls through to step 5. Note that "mismatch" includes the all-`0xFF`s state of a freshly erased chip — a brand-new RP2040 with empty flash falls into USB MSC mode automatically.
5. **USB MSC recovery mode.** The Boot ROM enables the USB device controller, enumerates as a Mass Storage Class device with vendor ID `0x2E8A` (Raspberry Pi) and product ID `0x0003`, with a single FAT12 partition named `RPI-RP2`. The drive is 128 MB nominal — the FAT12 layout pretends it is — but writes to it are intercepted: any UF2 file dropped on the drive is parsed, its payload bytes are written to the addresses the UF2 blocks declare, and the chip reboots when the file is complete. The drive also hosts two files that always exist: `INDEX.HTM` (redirects the user to raspberrypi.com) and `INFO_UF2.TXT` (prints the chip's unique ID and the SDK version that last flashed it). Datasheet §2.8.5, pp. 138–141, describes this mode in detail; the PICOBOOT vendor-specific interface on pp. 140–141 is what `picotool` talks to instead of the MSC layer when it needs read access to flash.

The Boot ROM does **not** run any user code in step 5. It is a self-contained USB MSC firmware that lives in ROM. You cannot modify it, replace it, or disable it. This is the chip's permanent recovery path — the reason that "I bricked my Pico" is, in practice, recoverable as long as you can still hold BOOTSEL during power-on.

## 3. Stage 1 — the 256-byte bootloader

If step 4 succeeded, the Boot ROM jumps to flash offset 0. The 256 bytes there must do one job: **configure the QSPI controller for execute-in-place reads so the rest of the flash becomes executable**, then jump to flash offset 256.

This is non-trivial. The QSPI controller (technically the XIP SSI — datasheet §4.10, pp. 600–649) supports many flash chips with many command sets, and the Pico's W25Q16JV-IQ uses the `0xEB` "Quad I/O Fast Read" instruction (Winbond W25Q16JV §7.2.16, p. 35) which needs to be:

1. Set the SSI's `CTRLR0` register for 4-bit-wide reads with appropriate `WAIT_CYCLES` (datasheet §4.10.13.1, p. 631).
2. Issue a one-time "enable Quad I/O continuous read mode" sequence so subsequent reads can start with the address bytes directly (saving ~2 µs per read).
3. Configure the XIP cache (datasheet §4.10.7, pp. 609–613) for write-back behavior.
4. Jump to `0x10000100` (the start of the application's vector table).

All of this in 252 bytes of ARMv6-M code (252 = 256 - 4 for the CRC trailer). The Pico SDK ships `boot2_w25q080.S` (~200 lines of assembly) which does it. Open `pico-sdk/src/rp2_common/boot_stage2/boot2_w25q080.S` and read it line by line — you will read it twice this week.

Key features of the Stage 1 bootloader:

- It is **position-independent** within its 256-byte window. The Boot ROM jumps to flash offset 0, but the linker places `boot_stage2` at `0x10000000`; the assembly is careful to use PC-relative loads (`ldr`) for any constants and to compute addresses by addition rather than absolute references.
- It uses **stack** at `0x20040000` (the top of SRAM) for one register save during the QSPI setup. The Boot ROM left the SP in a usable state but Stage 1 does not assume so; it explicitly sets SP before any push.
- The final instruction is `bx r0` where `r0` was loaded with `0x10000101` (the vector-table entry-point address, with the thumb bit set). Note the `+1` — Cortex-M instructions are always thumb; branching to an even address would fault with `INVSTATE` immediately.

The 4-byte CRC trailer is computed by `pico-sdk/src/rp2_common/boot_stage2/pad_checksum` (Python) and concatenated to the assembled output by the SDK's CMake. The CRC algorithm is CRC32 with the polynomial `0x4C11DB7u`, initial value `0xFFFFFFFFu`, no final XOR, MSB-first bit ordering — i.e., CRC32-MPEG-2. The Boot ROM checks this CRC at every cold boot. If you write a custom Stage 1 that omits the `pad_checksum` step, the chip will not boot.

## 4. Stage 2 — your bootloader (or your application)

At flash offset `0x10000100` (256 bytes after the start of flash), the Cortex-M0+ vector table for the next-stage code lives. The vector table is at minimum 16 entries × 4 bytes = 64 bytes:

- Entry 0: initial stack pointer (read into SP by the CPU at reset, but here it is read by Stage 1's final `bx`-equivalent).
- Entry 1: reset handler (the function that runs first).
- Entries 2–15: NMI handler, HardFault handler, SVCall, PendSV, SysTick, and IRQ handlers 0–N.

Stage 1's final action is equivalent to:

```c
uint32_t *vt = (uint32_t *)0x10000100;
__set_MSP(vt[0]);
((void (*)(void))vt[1])();
```

— set the main stack pointer to the value at offset 0, then call the function pointed to by offset 1.

For a standard Pico SDK application, the linker script `memmap_default.ld` places the vector table at `0x10000100` and the application's `main` runs after the reset handler initializes `.data` and `.bss`. For our bootloader, **we put our bootloader's vector table at `0x10000100`** and the bootloader runs first. The bootloader, when it decides to jump to the application, replays the Stage 1 → Stage 2 transition manually but with the application's vector-table address (`0x10008100` in our layout):

```c
static void boot_application(uint32_t app_vt_address) {
    /* Disable interrupts. */
    __disable_irq();

    /* Read SP and PC from the application's vector table. */
    uint32_t app_sp = *((const uint32_t *)(app_vt_address + 0u));
    uint32_t app_pc = *((const uint32_t *)(app_vt_address + 4u));

    /* Relocate the vector-table offset register so the application's
       VTOR points at its own table. SCB->VTOR lives in the System
       Control Block. */
    SCB->VTOR = app_vt_address;

    /* Synchronize before the jump. */
    __DSB();
    __ISB();

    /* Set the main stack pointer and jump. */
    __set_MSP(app_sp);
    ((void (*)(void))app_pc)();

    /* Unreachable. */
    for (;;) { __WFI(); }
}
```

Read this snippet carefully — it is the entire mechanism by which a bootloader hands control to an application. The application's reset handler then does its own `.data`/`.bss` setup as if it had been booted directly. The fact that there is a bootloader between the silicon and the application is invisible to the application unless the bootloader has left a "swap status" flag in metadata for the application to read.

The `SCB->VTOR` register (System Control Block, Vector Table Offset Register — see ARMv6-M Architecture Reference Manual §B3.2.5, free at <https://developer.arm.com/documentation/ddi0419/latest/>) determines where the CPU looks for interrupt vectors. Without setting `VTOR` to the application's vector-table base, any interrupt that fires inside the application would dispatch through the bootloader's still-resident vectors — which point to functions inside the bootloader's `.text`, which may no longer make sense.

## 5. The flash layout we will use

```text
+------------------------------------------------------------+
| Address               | Size  | Contents                   |
+-----------------------+-------+----------------------------+
| 0x10000000-0x100000FF |  256B | Stage 1 (boot2_w25q080)    |
| 0x10000100-0x100080FF | ~32K  | Bootloader                 |
| 0x10008100-0x100C40FF | ~752K | Active application bank    |
| 0x100C4100-0x1017FFFF | ~752K | Staging application bank   |
| 0x10180000-0x1017FFFF |   16K | Reserved (alignment slack) |
| 0x101FC000-0x101FFFFF |   16K | Metadata (4 KB pages × 4)  |
+-----------------------+-------+----------------------------+
```

A few notes:

- The Pico has 2 MB total flash (`0x10000000` to `0x101FFFFF`). The exact upper bound varies by board — the Pico W has 2 MB; the Pico 1 has 2 MB; some clones have 16 MB. For our project we assume 2 MB.
- The active and staging banks are each ~752 KB which is the largest power-of-2-aligned region that fits two copies plus the 32 KB bootloader plus the 16 KB metadata. A real product would size these to the actual maximum firmware size (often 256 KB or 512 KB) and reclaim the remainder for a filesystem partition.
- The metadata region is 16 KB = four 4 KB sectors. We use two of them in a "ping-pong" pattern (one active, one staging-the-next-state) so the metadata write is power-loss-safe; the bootloader picks whichever sector has a valid CRC and the higher sequence number. The other two sectors are reserved for future use.
- The 32 KB bootloader budget is generous. Our actual bootloader compiles to ~28 KB with `-Os` and the verify-only Ed25519 included. If we needed more headroom, we could move the application's bank start to `0x1000C100` (giving the bootloader 48 KB) and shorten the staging bank correspondingly.

The linker scripts that produce this layout are:

- `memmap_bootloader.ld` — places `.text` at `0x10000100`, `.data`/`.bss` at SRAM start. Memory region `FLASH` is `ORIGIN = 0x10000100, LENGTH = 0x8000` (32 KB).
- `memmap_app.ld` — places `.text` at `0x10008100`. Memory region `FLASH` is `ORIGIN = 0x10008100, LENGTH = 0xBC000` (752 KB).

We will look at these in Lecture 2.

## 6. The UF2 file format

UF2 is Microsoft's drag-and-drop firmware format. The spec is one README at <https://github.com/microsoft/uf2#file-format>; the relevant parts are 96 lines.

A UF2 file is a sequence of **512-byte blocks**. Each block is self-contained — you can take a UF2 file, split it in the middle of a block boundary, and either half is independently valid. This property is what lets the Boot ROM's USB MSC firmware accept a UF2 incrementally without buffering the entire file.

Each block has three sections:

```text
+---------+----------------------------------------+--------+
| Offset  | Field                                   | Size   |
+---------+-----------------------------------------+--------+
|   0     | magicStart0 = 0x0A324655u ("UF2\n")     | 4 B    |
|   4     | magicStart1 = 0x9E5D5157u               | 4 B    |
|   8     | flags                                   | 4 B    |
|  12     | targetAddr                              | 4 B    |
|  16     | payloadSize                             | 4 B    |
|  20     | blockNo                                 | 4 B    |
|  24     | numBlocks                               | 4 B    |
|  28     | fileSize_or_familyID                    | 4 B    |
|  32     | data (476 bytes; first payloadSize used)| 476 B  |
| 508     | magicEnd = 0x0AB16F30u                  | 4 B    |
+---------+-----------------------------------------+--------+
```

All multi-byte fields are little-endian. The three magic numbers are constant. `flags` is a bitmask:

- `0x00000001u` — *not main flash*: this block is for a partition that is not the chip's main flash (e.g. EEPROM, external SPI). Almost never set in practice.
- `0x00001000u` — *file container*: this UF2 contains files in a filesystem-format-agnostic way. Rarely used.
- `0x00002000u` — *familyID present*: `fileSize_or_familyID` should be interpreted as a familyID. The RP2040 family ID is `0xE48BFF56u`. This flag is **always set** on RP2040 UF2 files.
- `0x00004000u` — *md5 checksum present*: a 24-byte MD5 hash trailer is appended after the data. Not used for RP2040.
- `0x00008000u` — *extension tags present*: optional metadata follows the payload. Not used for RP2040.

`targetAddr` is the flash address this block's payload should be written to (`0x10000000`-based for RP2040). `payloadSize` is how many bytes of the 476-byte `data` field are valid (always 256 for RP2040). `blockNo` is the block's index in this file (zero-based); `numBlocks` is the total block count.

`fileSize_or_familyID` is, for RP2040, the family ID `0xE48BFF56u`. The full registry of family IDs is at <https://github.com/microsoft/uf2/blob/master/utils/uf2families.json> — currently ~30 entries (Atmel SAMD21, SAMD51, nRF52840, ESP32 variants, ESP8266, STM32 variants, RP2040, RP2350, BBC micro:bit V2, several Adafruit-specific IDs).

### A worked example

Consider a tiny "blink the LED" program with 1024 bytes of code. Its UF2 file would have `1024 / 256 = 4` blocks, each containing 256 bytes of payload. The header for block 0:

```text
magicStart0   = 0x0A324655  (little-endian: 55 46 32 0A)
magicStart1   = 0x9E5D5157  (little-endian: 57 51 5D 9E)
flags         = 0x00002000  (familyID present)
targetAddr    = 0x10000100  (vector table starts here)
payloadSize   = 0x00000100  (= 256)
blockNo       = 0x00000000
numBlocks     = 0x00000004
familyID      = 0xE48BFF56  (RP2040)
data          = (256 bytes of code, then 220 bytes of 0x00)
magicEnd      = 0x0AB16F30  (little-endian: 30 6F B1 0A)
```

Total file size: `4 × 512 = 2048 bytes` for 1024 bytes of program. The 100% overhead is acceptable because UF2 files are not transmitted over expensive channels — they go onto a 128 MB virtual drive that the Boot ROM ignores most of.

### The parser

Exercise 1 asks you to write a UF2 parser in C. The algorithm is:

```c
typedef struct {
    uint32_t target_addr;
    uint32_t length;
    const uint8_t *data;
} uf2_block_payload_t;

int uf2_parse_block(const uint8_t *block, uf2_block_payload_t *out) {
    /* Read header fields with explicit little-endian loads. */
    uint32_t m0 = read_le32(block + 0);
    uint32_t m1 = read_le32(block + 4);
    uint32_t flags = read_le32(block + 8);
    uint32_t target = read_le32(block + 12);
    uint32_t plen = read_le32(block + 16);
    uint32_t blk_no = read_le32(block + 20);
    uint32_t blk_count = read_le32(block + 24);
    uint32_t fid = read_le32(block + 28);
    uint32_t end = read_le32(block + 508);

    if (m0 != UF2_MAGIC_START0_VALUE) return UF2_ERR_BAD_MAGIC0;
    if (m1 != UF2_MAGIC_START1_VALUE) return UF2_ERR_BAD_MAGIC1;
    if (end != UF2_MAGIC_END_VALUE)   return UF2_ERR_BAD_MAGIC_END;
    if (plen > 476u)                  return UF2_ERR_PAYLOAD_TOO_BIG;
    if ((flags & UF2_FLAG_FAMILYID_PRESENT) == 0u) return UF2_ERR_NO_FAMILY;
    if (fid != UF2_FAMILY_RP2040)     return UF2_ERR_WRONG_FAMILY;
    if (blk_no >= blk_count)          return UF2_ERR_BAD_BLOCK_NUMBER;

    out->target_addr = target;
    out->length      = plen;
    out->data        = block + 32;
    return UF2_OK;
}
```

The header constants (`UF2_MAGIC_START0_VALUE`, etc.) live in `bootloader_common.h`. The full parser plus a "concatenate all blocks into a contiguous buffer" pass is ~150 lines including error handling. Exercise 1's reference solution is in `exercises/SOLUTIONS.md`.

### Why UF2 and not raw .bin?

The Boot ROM's USB MSC firmware needs to know **where** to write the bytes it receives. A raw `.bin` carries only data, not addresses; the host writes it at offset 0 of the drive, which the Boot ROM has no way to map to a flash address. UF2 puts the target address in every block header, so the Boot ROM treats each block independently: "write these 256 bytes at this address". This also means UF2 files can have *holes* (block N targets `0x10000100`, block N+1 targets `0x10010000`, skipping 64 KB), which is useful when the ELF being converted has multiple non-contiguous sections.

The Pico SDK's `elf2uf2` tool (in `pico-sdk/tools/elf2uf2/`) reads an ELF, iterates its `PT_LOAD` segments, splits each segment into 256-byte chunks, and emits one UF2 block per chunk. It is ~300 lines of C++ and worth reading.

## 7. The Boot ROM's USB MSC firmware as a recovery anchor

When you finish writing the bootloader on Friday, you will brick it at least once. The recovery path is always:

1. Disconnect the Pico from USB.
2. Hold BOOTSEL.
3. Reconnect USB while still holding BOOTSEL.
4. Release BOOTSEL after the host enumerates `RPI-RP2`.
5. Drag a known-good UF2 onto the drive.

This works **regardless of what is in flash**, because step 1 of the cold-reset sequence runs from mask ROM and is unaffected by flash contents. The Boot ROM's USB MSC firmware is the chip's lifeline; we will lean on it heavily.

The programmatic equivalent is `picotool reboot -u -f` (which uses the `WATCHDOG_SCRATCH4 = 0xB007C0D3` trick described in §2). This works only when the current firmware is responsive enough to receive a `picotool` reboot command — if the firmware is locked up, you must use the physical button.

A more advanced recovery anchor: our bootloader, if both active and staging images fail signature verification, **calls the Boot ROM's USB MSC entry point directly** to drop into recovery mode without requiring the user to power-cycle. The entry point is documented in datasheet §2.8.5 on p. 138 — there is a function `reset_to_usb_boot(uint32_t gpio_pin_mask, uint32_t disable_interface_mask)` exposed via the Boot ROM function table at table-code `('U', 'B')`. Calling this is the bootloader's last resort:

```c
typedef void (*usb_boot_fn_t)(uint32_t, uint32_t);

static void __attribute__((noreturn)) drop_to_usb_msc(void) {
    rom_table_lookup_fn lookup =
        (rom_table_lookup_fn) rom_hword_as_ptr(0x18);
    usb_boot_fn_t reset_to_usb_boot =
        (usb_boot_fn_t) lookup(rom_table_code('U', 'B'));
    reset_to_usb_boot(0u /* no LED */, 0u /* both interfaces enabled */);
    for (;;) { }
}
```

This means our bootloader is **brick-proof from the bootloader's own perspective**. As long as the bootloader itself is intact, a user with no application image at all still sees the BOOTSEL drive on USB and can recover. The only way to truly brick the device is to corrupt the bootloader, which the Pico SDK's flashing tools cannot do accidentally (they always write the UF2 to the addresses the UF2 names, and the bootloader's UF2 is what the user dropped, so unless the user drops a bad bootloader UF2, the bootloader stays intact).

## 8. Summary

The three-stage boot is the entire mental model. Stage 0 in ROM you cannot change. Stage 1 in your flash[0..255], which the Boot ROM CRC-checks. Stage 2 at flash[256+], which is either your bootloader (this week's project) or your application directly (every previous week). The UF2 format is the file format the recovery path consumes. Everything else this week — dual banks, signed images, OTA — is layered on top of these foundations.

Next lecture: the Boot ROM API for flash erase/program, the metadata page design for the dual-bank state machine, and the linker scripts that put the bootloader and the application at the right flash addresses.

## References for this lecture

- RP2040 datasheet §2.8 "Bootrom", pp. 130–141, especially §2.8.1 (sequence), §2.8.3 (API), §2.8.5 (USB MSC). <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>
- RP2040 datasheet §2.6.1 "Address Map", p. 24.
- RP2040 datasheet §4.10 "QSPI", pp. 600–649.
- Winbond W25Q16JV datasheet, §7.2.16 "Quad I/O Fast Read", p. 35. <https://www.winbond.com/resource-files/W25Q16JV_DTR_RevD%2008292016.pdf>
- ARMv6-M Architecture Reference Manual §B3.2.5 "Vector Table Offset Register", free at <https://developer.arm.com/documentation/ddi0419/latest/>.
- Microsoft UF2 spec README, "File format" section. <https://github.com/microsoft/uf2#file-format>
- Pico SDK source: `src/rp2_common/boot_stage2/boot2_w25q080.S`, `tools/elf2uf2/`.
