# Lecture 2 — Dual-Bank Flash Layout and the Boot ROM API

> *Flash memory is a peculiar storage medium. You can read it like RAM and your code does, on the RP2040, through the XIP cache that the silicon engineers wrote for you. You can write it but only after you have erased it, and you erase it in chunks of 4 KB at a time, and during the erase the chip is unable to read its own program — so the code running the erase must not live in the flash you are erasing, and on a single-bank flash chip with execute-in-place that means you must copy the writer into SRAM, mask interrupts so the ISR vectors do not get fetched from a now-dead flash, run the writer from RAM, return to the SRAM stub when the writer is done, and only then re-enter XIP. The Boot ROM's flash API exists precisely so you do not have to write that dance yourself. This lecture is about the dance, the API that hides it, and the flash layout you will write your bootloader against.*

## 1. The flash chip's vocabulary

The Pico's flash is a Winbond W25Q16JV-IQ — 16 megabits, 2 MB of usable storage, QSPI interface up to 133 MHz, 4 KB sector erase, 256-byte page program. Read the Winbond datasheet's §6.1 ("Standard SPI Commands") and §7 ("Standard SPI Instructions") for the full picture; the operations our bootloader cares about are:

- **Read (read while in XIP mode):** the QSPI controller issues `0xEB` "Quad I/O Fast Read" (§7.2.16, p. 35). Our code does not see this; the XIP controller does. The flash answers at up to 133 MHz × 4 bits = 66 MB/s on paper, ~30 MB/s in practice on the Pico (limited by the XIP cache architecture).
- **Sector erase:** `0x20` "Sector Erase 4KB" (§7.2.18, p. 37). Erases 4096 bytes aligned to a 4 KB boundary. Sets all bits in the sector to 1 (i.e., bytes become `0xFF`). The erase takes ~45 ms typical, ~400 ms maximum (W25Q16JV §10.2, p. 65, "Sector Erase Time"). During the erase the chip is *busy* — any read returns the previous contents of an unrelated address with the status-register's BUSY bit set; the QSPI controller, if asked to read during a busy state, retries and stalls the bus. Practical effect: code running from flash freezes during an erase, which is why erase must run from RAM.
- **Page program:** `0x02` "Page Program" (§7.2.10, p. 32). Writes 1–256 bytes within a single 256-byte page. The write *clears* bits — it can change 1 to 0 but not 0 to 1 (to set a 0 back to 1 you must erase the whole sector). The write takes ~0.4 ms typical, ~3 ms maximum (W25Q16JV §10.2, p. 65, "Page Program Time"). During the program the chip is busy in the same sense as erase.
- **Write enable:** `0x06` "Write Enable" (§7.2.4, p. 22). Must be issued before every erase or program; the chip clears the write-enable latch after each erase/program operation, so a multi-page write requires `WREN` before each page.

The 4 KB erase + 256 B program asymmetry has consequences. To rewrite a single byte at offset `0x10008500`, you must: read the entire 4 KB sector starting at `0x10008000` into SRAM, modify the byte, erase the sector, program 16 pages (256 bytes each) back. The flash-write helper in the Pico SDK (`flash_range_program`) does **not** do this for you — it assumes the destination is already erased. We always erase first, program second, and design our metadata layout so we never need to modify a single byte in place.

## 2. The Boot ROM's flash API

The Boot ROM exposes a small set of functions at known table-codes. The table is at `*((uint16_t *)0x14)` — a half-word at flash offset 0x14, read as a pointer (datasheet §2.8.3.1, p. 132). The function-lookup routine itself is at `*((uint16_t *)0x18)` cast to `rom_table_lookup_fn`. The Pico SDK wraps the lookup as `rom_func_lookup(rom_table_code(a, b))` where `(a, b)` is a 2-byte function ID.

The functions our bootloader uses (datasheet Table 167, pp. 132–134):

| Table code | C signature                                                                | Purpose                                              |
|------------|----------------------------------------------------------------------------|------------------------------------------------------|
| `('I', 'F')` | `void _connect_internal_flash(void)`                                       | Re-route the QSPI pads to be controlled by the SSI   |
| `('E', 'X')` | `void _flash_exit_xip(void)`                                               | Leave XIP mode (no more reads from flash, prepare for programming) |
| `('R', 'E')` | `void _flash_range_erase(uint32_t addr, size_t count, uint32_t block_size, uint8_t cmd)` | Erase `count` bytes starting at `addr` (flash-relative — addr 0 is flash offset 0, not `0x10000000`) using `cmd` as the erase opcode and `block_size` as the alignment of one erase command |
| `('R', 'P')` | `void _flash_range_program(uint32_t addr, const uint8_t *data, size_t count)` | Program `count` bytes from `data` into flash at flash-relative address `addr` |
| `('F', 'C')` | `void _flash_flush_cache(void)`                                            | Evict the XIP cache after a write (so subsequent reads see the new bytes, not the cached old bytes) |
| `('C', 'X')` | `void _flash_enter_cmd_xip(void)`                                          | Re-enter XIP mode (using the slower `0x03` Read instruction; sufficient for the bootloader's purposes) |

A full erase-and-program sequence at the application level looks like:

```c
#include "pico/bootrom.h"
#include "hardware/sync.h"

typedef void (*flash_range_erase_fn_t)(uint32_t, size_t, uint32_t, uint8_t);
typedef void (*flash_range_program_fn_t)(uint32_t, const uint8_t *, size_t);
typedef void (*flash_connect_fn_t)(void);
typedef void (*flash_void_fn_t)(void);

static void flash_safe_write(uint32_t flash_offset,
                             const uint8_t *src,
                             size_t length) {
    flash_void_fn_t connect_internal_flash =
        (flash_void_fn_t) rom_func_lookup(rom_table_code('I', 'F'));
    flash_void_fn_t flash_exit_xip =
        (flash_void_fn_t) rom_func_lookup(rom_table_code('E', 'X'));
    flash_range_erase_fn_t flash_range_erase =
        (flash_range_erase_fn_t) rom_func_lookup(rom_table_code('R', 'E'));
    flash_range_program_fn_t flash_range_program =
        (flash_range_program_fn_t) rom_func_lookup(rom_table_code('R', 'P'));
    flash_void_fn_t flash_flush_cache =
        (flash_void_fn_t) rom_func_lookup(rom_table_code('F', 'C'));
    flash_void_fn_t flash_enter_cmd_xip =
        (flash_void_fn_t) rom_func_lookup(rom_table_code('C', 'X'));

    uint32_t ints = save_and_disable_interrupts();

    connect_internal_flash();
    flash_exit_xip();
    flash_range_erase(flash_offset, length, 4096u, 0x20u);
    flash_range_program(flash_offset, src, length);
    flash_flush_cache();
    flash_enter_cmd_xip();

    restore_interrupts(ints);
}
```

A few notes on this code:

- **Interrupts must be masked** for the duration of the write. The XIP cache being disabled means any ISR vector fetch would read garbage and HardFault; even briefly leaving interrupts enabled during the erase risks an interrupt firing mid-erase. The SDK's `save_and_disable_interrupts` writes `PRIMASK` to mask all but NMI/HardFault.
- **The functions are looked up once per write.** A more careful implementation looks them up once at startup and caches the function pointers; the cost of the lookup is small (~10 µs each) but matters during the multi-second OTA flash phase.
- **`flash_offset` is flash-relative**, not memory-mapped. To erase the staging bank that begins at `0x100C4100` (memory-mapped), the `flash_offset` argument is `0x000C4100` (memory-mapped minus `0x10000000`). This is the most common bug; if you pass `0x100C4100` directly, the API silently erases sectors at flash offset `0x100C4100` which is **outside the flash address space** and the API either no-ops or wraps. Either way, your write does not land where you expect.
- **The erase length must be a multiple of 4 KB** and **the erase address must be 4 KB aligned**. If you call with a length of 4097 bytes, the API rounds up to 8 KB; if you call with an address of `0x100C4108`, the API rounds down to `0x100C4000`. Both are bug magnets. Always pre-compute `flash_offset = ROUND_DOWN(addr, 4096)` and `length = ROUND_UP(end_addr, 4096) - flash_offset` before the call.
- **The program length must be a multiple of 256 bytes** and **the program address must be 256-byte aligned**. The SDK's `flash_range_program` will not pad for you; if you have 250 bytes to write, you must allocate a 256-byte buffer, zero-pad it, and write the full buffer.

## 3. The bootloader's flash-write helper

Because the bootloader cannot link the SDK (size budget), we reimplement the helper from scratch. Our version, in `mini-project/bootloader.c`, is functionally identical to the snippet above but uses pre-cached function pointers and an explicit `noinline` attribute on the writer function (because the writer must be in SRAM for the duration of the operation; the Pico SDK's `__not_in_flash_func` macro arranges this, and we use the same macro). The full helper is 80 lines including error checks.

The bootloader allocates the function pointers in a static struct at startup:

```c
typedef struct {
    void (*connect_internal_flash)(void);
    void (*flash_exit_xip)(void);
    void (*flash_range_erase)(uint32_t addr, size_t count, uint32_t block_size, uint8_t cmd);
    void (*flash_range_program)(uint32_t addr, const uint8_t *data, size_t count);
    void (*flash_flush_cache)(void);
    void (*flash_enter_cmd_xip)(void);
} bootloader_flash_api_t;

static bootloader_flash_api_t g_flash_api;

static void flash_api_init(void) {
    rom_table_lookup_fn lookup =
        (rom_table_lookup_fn) rom_hword_as_ptr(0x18u);

    g_flash_api.connect_internal_flash =
        (void (*)(void)) lookup(rom_table_code('I', 'F'));
    g_flash_api.flash_exit_xip =
        (void (*)(void)) lookup(rom_table_code('E', 'X'));
    g_flash_api.flash_range_erase =
        (void (*)(uint32_t, size_t, uint32_t, uint8_t)) lookup(rom_table_code('R', 'E'));
    g_flash_api.flash_range_program =
        (void (*)(uint32_t, const uint8_t *, size_t)) lookup(rom_table_code('R', 'P'));
    g_flash_api.flash_flush_cache =
        (void (*)(void)) lookup(rom_table_code('F', 'C'));
    g_flash_api.flash_enter_cmd_xip =
        (void (*)(void)) lookup(rom_table_code('C', 'X'));
}
```

This runs once in the bootloader's `main` before any flash work.

## 4. The dual-bank layout in detail

```text
+------------------+-----------------+---------+--------------------+
| Memory address   | Flash offset    | Size    | Region             |
+------------------+-----------------+---------+--------------------+
| 0x10000000       | 0x00000000      |   256 B | Stage 1 boot2      |
| 0x10000100       | 0x00000100      | 32256 B | Bootloader text    |
| 0x10008000       | 0x00008000      |   256 B | (alignment slack)  |
| 0x10008100       | 0x00008100      | 770304 B| Active bank        |
| 0x100C4100       | 0x000C4100      | 770304 B| Staging bank       |
| 0x1017F100       | 0x0017F100      | 3840 B  | (alignment slack)  |
| 0x101FC000       | 0x001FC000      |  4 KB   | Metadata page A    |
| 0x101FD000       | 0x001FD000      |  4 KB   | Metadata page B    |
| 0x101FE000       | 0x001FE000      |  4 KB   | Reserved page C    |
| 0x101FF000       | 0x001FF000      |  4 KB   | Reserved page D    |
+------------------+-----------------+---------+--------------------+
```

A few points:

- The bootloader's text region ends at `0x10007FFF` (32 KB after the start). The 256 bytes of "alignment slack" between `0x10008000` and `0x10008100` ensure the application's vector table at `0x10008100` is on a 256-byte boundary (the Cortex-M0+'s `VTOR` requires this — datasheet §4.4.5, p. 80, "VTOR register").
- The active and staging banks are each 770,304 bytes = ~752 KB. The number is chosen as `(2 MB - 32 KB bootloader - 16 KB metadata - 256 B boot2) / 2`, rounded to a sector multiple.
- The metadata region is two ping-pong pages plus two reserved. We use the lower 4 KB of each 4 KB page for the metadata struct and leave the remainder for future fields.

## 5. The metadata struct

The metadata describes the bootloader's state and the staging image's properties. It is 4 KB long (one sector) but uses only ~100 bytes; the rest is padded with `0xFF` (the erased state).

```c
typedef struct {
    /* Magic identifier so the bootloader knows this page is valid metadata. */
    uint32_t magic;            /* 0xCCC10BEEu — Code Crunch C10 boot-eeprom magic */

    /* Monotonically increasing sequence number; the page with the higher
       valid sequence is the active page. */
    uint32_t sequence;

    /* Current state of the bootloader's swap state machine. */
    uint32_t state;            /* see enum below */

    /* Pointer to the active bank's start. Constant in our build; included
       for future flexibility. */
    uint32_t active_addr;

    /* Pointer to the staging bank's start. Constant in our build. */
    uint32_t staging_addr;

    /* Size of the firmware in the staging bank, in bytes (excluding signed
       image header and signature). */
    uint32_t staging_size;

    /* SHA-256 of the staging firmware, including the signed-image header
       but excluding the signature. */
    uint8_t staging_sha256[32];

    /* Ed25519 signature of the staging firmware. */
    uint8_t staging_signature[64];

    /* Watchdog confirmation flag. Set by the application after first
       successful boot; checked by the bootloader on the next reset. */
    uint32_t boot_confirmed;

    /* Number of times the bootloader has booted this active image without
       a confirmation. Reset on confirmation. If > N, fall back. */
    uint32_t boot_attempts;

    /* CRC32 of all fields above. The bootloader rejects pages whose CRC
       does not match. */
    uint32_t crc32;
} bootloader_metadata_t;

enum bootloader_state {
    BL_STATE_IDLE             = 0u, /* No update pending; boot active. */
    BL_STATE_SWAP_REQUESTED   = 1u, /* Application asked for a swap on next boot. */
    BL_STATE_SWAP_IN_PROGRESS = 2u, /* Bootloader was interrupted mid-swap; resume. */
    BL_STATE_BOOTING_NEW      = 3u, /* Active is the new firmware; awaiting confirmation. */
    BL_STATE_ROLLBACK_PENDING = 4u, /* Confirmation failed; fall back on next boot. */
};
```

The struct is 156 bytes including padding. We use 156 bytes out of the 4 KB sector. The remaining 3940 bytes of the sector are `0xFF` and ignored.

## 6. The ping-pong write protocol

Single-sector metadata has a fatal problem: a power loss mid-erase wipes the metadata to all-`0xFF`s and the bootloader has no idea what state it was in. We use two sectors and a sequence number so there is always one valid metadata page:

1. Bootloader at startup reads both metadata sectors A and B.
2. For each sector: check `magic`, check `crc32` matches the struct contents, treat as valid if both pass.
3. If both valid: pick the one with the higher `sequence`.
4. If only A is valid: use A.
5. If only B is valid: use B.
6. If neither valid: this is the first boot ever; initialize state to `BL_STATE_IDLE` and write to A with `sequence = 0`.

Writes follow the inverse pattern:

1. Read the current state from the active sector (say A).
2. Construct the new metadata struct with `sequence = old.sequence + 1`.
3. Compute `crc32` over the struct.
4. Erase the *inactive* sector (B).
5. Program the new struct into B.
6. Verify the write by reading B back and comparing.
7. Now B is the new active sector; A is the old one.

A power loss between step 4 (erase B) and step 7 (B committed) leaves A intact with the old `sequence`, so on reboot the bootloader picks A and proceeds as if the write never happened. This is the "atomic commit" guarantee. The mathematical condition for correctness: the bootloader's "pick the higher valid sequence" rule must be deterministic and tolerant of one invalid page; both are trivially provable from the algorithm.

## 7. The linker scripts

The Pico SDK's default linker script `pico-sdk/src/rp2_common/pico_standard_link/memmap_default.ld` is the starting point. We make two derivatives.

### `memmap_bootloader.ld`

```ld
MEMORY
{
    FLASH(rx)       : ORIGIN = 0x10000000, LENGTH = 32k
    /* The first 256 bytes are reserved for the second-stage bootloader. */
    RAM(rwx)        : ORIGIN = 0x20000000, LENGTH = 256k
    SCRATCH_X(rwx)  : ORIGIN = 0x20040000, LENGTH = 4k
    SCRATCH_Y(rwx)  : ORIGIN = 0x20041000, LENGTH = 4k
}

ENTRY(_entry_point)

SECTIONS
{
    /* Second-stage bootloader is prepended to image by tools at build time. */
    .flash_begin : {
        __flash_binary_start = .;
    } > FLASH

    .boot2 : {
        __boot2_start__ = .;
        KEEP (*(.boot2))
        __boot2_end__ = .;
    } > FLASH

    ASSERT(__boot2_end__ - __boot2_start__ == 256,
        "ERROR: Pico second stage bootloader must be 256 bytes in size")

    .text : {
        __logical_binary_start = .;
        KEEP (*(.vectors))
        KEEP (*(.binary_info_header))
        __binary_info_header_end = .;
        KEEP (*(.reset))
        *(.init)
        *(EXCLUDE_FILE(*libgcc.a:*) .text*)
        *(.fini)
        /* ... standard glue ... */
    } > FLASH

    /* Standard .rodata, .data, .bss, stack regions follow. */
    /* The bootloader's total flash usage must not exceed 32 KB. */
}
```

Key differences from `memmap_default.ld`:

- `FLASH` length is 32 KB instead of 2 MB. The linker errors at build time if the bootloader exceeds 32 KB.
- The application uses `memmap_app.ld` (below) which places `.text` at offset 32 KB into flash; our bootloader's link-time origin remains `0x10000000` so the second-stage bootloader at flash[0..255] and the bootloader's vector table at flash[256] are correctly laid out.

### `memmap_app.ld`

```ld
MEMORY
{
    FLASH(rx)       : ORIGIN = 0x10008100, LENGTH = 752k
    RAM(rwx)        : ORIGIN = 0x20000000, LENGTH = 256k
    SCRATCH_X(rwx)  : ORIGIN = 0x20040000, LENGTH = 4k
    SCRATCH_Y(rwx)  : ORIGIN = 0x20041000, LENGTH = 4k
}

ENTRY(_entry_point)

SECTIONS
{
    .flash_begin : {
        __flash_binary_start = .;
    } > FLASH

    /* No .boot2 here; the application is downstream of the bootloader. */

    .text : {
        __logical_binary_start = .;
        KEEP (*(.vectors))
        KEEP (*(.binary_info_header))
        __binary_info_header_end = .;
        KEEP (*(.reset))
        *(.init)
        *(EXCLUDE_FILE(*libgcc.a:*) .text*)
        *(.fini)
    } > FLASH

    /* Standard .rodata, .data, .bss, stack regions follow. */
}
```

The application's `ORIGIN` is `0x10008100` — the start of the active bank. The application's vector table lands at this address, which is where the bootloader's `boot_application` function jumps.

CMake plumbing: in the bootloader's `CMakeLists.txt`, add `pico_set_linker_script(bootloader ${CMAKE_CURRENT_SOURCE_DIR}/memmap_bootloader.ld)`. In the application's `CMakeLists.txt`, add `pico_set_linker_script(application ${CMAKE_CURRENT_SOURCE_DIR}/memmap_app.ld)`. The application also needs `target_compile_definitions(application PRIVATE PICO_FLASH_SIZE_BYTES=770304)` so the SDK's flash-utility code stays within bounds.

## 8. The `boot_application` jump in detail

Once the bootloader has decided which bank to boot (active, after possibly copying staging to active first), it transfers control with the snippet from Lecture 1 — repeated here with full context:

```c
#define APP_VECTOR_TABLE_ADDR 0x10008100u

static void __attribute__((noreturn)) boot_application(void) {
    /* Disable interrupts. */
    __disable_irq();

    /* Read SP and PC from the application's vector table. */
    const uint32_t *vt = (const uint32_t *) APP_VECTOR_TABLE_ADDR;
    uint32_t app_sp = vt[0];
    uint32_t app_pc = vt[1];

    /* Sanity-check the SP — it should point into SRAM. If the application
       is corrupted and SP is bogus, branching to it will fault immediately
       on the first push. */
    if (app_sp < 0x20000000u || app_sp > 0x20042000u) {
        /* Application is corrupt. Fall through to recovery. */
        drop_to_usb_msc();
    }

    /* Sanity-check the PC — it should point into flash with the thumb bit set. */
    if ((app_pc & 0x10000000u) == 0u || (app_pc & 0x1u) == 0u) {
        drop_to_usb_msc();
    }

    /* Relocate VTOR. */
    *((volatile uint32_t *) 0xE000ED08u) = APP_VECTOR_TABLE_ADDR;

    /* Synchronize. */
    __DSB();
    __ISB();

    /* Set the main stack pointer and jump. */
    __set_MSP(app_sp);
    ((void (*)(void))app_pc)();

    /* Unreachable. */
    for (;;) { __WFI(); }
}
```

The two sanity checks (SP in SRAM range, PC in flash range with thumb bit) are the bootloader's cheapest defense against a corrupt application. They catch the all-`0xFF` case (where SP would be `0xFFFFFFFF`u, way above SRAM) and the all-zero case (PC would be `0u`, no thumb bit). They do **not** catch a deliberately-crafted application that points at executable bytes; that is what signature verification (Lecture 3) is for.

## 9. The reset-cause inspection

When the bootloader starts, it inspects the watchdog scratch registers to learn why the chip rebooted. The SDK exposes this via `watchdog_caused_reboot()` and `watchdog_enable_caused_reboot()`. We also read `WATCHDOG_SCRATCH4` directly:

```c
enum reset_cause {
    RESET_CAUSE_COLD,           /* Power-on or RUN pin */
    RESET_CAUSE_WATCHDOG,       /* Watchdog timeout */
    RESET_CAUSE_REQUESTED_SWAP, /* Application set scratch4 = 0xCCC10A55 */
    RESET_CAUSE_REQUESTED_USB,  /* Application set scratch4 = 0xB007C0D3 */
};

static enum reset_cause identify_reset_cause(void) {
    /* WATCHDOG_SCRATCH4 = 0xCCC10A55 means the application reset us and
       wants us to perform a swap. */
    if (watchdog_hw->scratch[4] == 0xCCC10A55u) {
        watchdog_hw->scratch[4] = 0u;
        return RESET_CAUSE_REQUESTED_SWAP;
    }
    if (watchdog_hw->scratch[4] == 0xB007C0D3u) {
        watchdog_hw->scratch[4] = 0u;
        return RESET_CAUSE_REQUESTED_USB;
    }
    if (watchdog_caused_reboot()) {
        return RESET_CAUSE_WATCHDOG;
    }
    return RESET_CAUSE_COLD;
}
```

The `RESET_CAUSE_REQUESTED_USB` case is the one the application sets when the user runs a `cc-flash --recover` command: the application sets scratch4 to the Boot ROM's USB-recovery magic and triggers a watchdog reset. The bootloader sees this, immediately drops to `drop_to_usb_msc()` without trying to boot any application. This gives the user a "force recovery" command without requiring the BOOTSEL button.

## 10. Summary

Flash erase is 4 KB sectors; flash program is 256-byte pages. The Boot ROM exposes the QSPI dance as six function pointers. The bootloader pre-caches these at startup. The dual-bank layout puts active and staging banks of 752 KB each on either side of the bootloader's 32 KB region, with 16 KB of metadata at the top of flash split into a ping-pong pair plus two reserved sectors. The linker scripts enforce the layout. The metadata's atomic-commit comes from the ping-pong write order: erase the inactive sector, write the new state with `sequence + 1`, the bootloader's "pick the higher valid sequence" rule recovers from any power loss.

Next lecture: the Ed25519 signature math, the signed-image file format, the OTA protocol over CDC, and the watchdog-confirmation anti-bricking pattern.

## References for this lecture

- RP2040 datasheet §2.8.3 "Bootrom API", pp. 132–135.
- RP2040 datasheet §4.10 "QSPI", pp. 600–649.
- RP2040 datasheet §4.4.5 "VTOR register", p. 80.
- RP2040 datasheet §4.7.6 "WATCHDOG_REASON", p. 562.
- Winbond W25Q16JV §7.2.10 "Page Program", p. 32; §7.2.18 "Sector Erase", p. 37; §10.2 "AC Characteristics", p. 65.
- Pico SDK source: `src/rp2_common/hardware_flash/flash.c`, `src/rp2_common/pico_standard_link/memmap_default.ld`.
- MCUboot design doc, "Slot partitions" section. <https://docs.mcuboot.com/design.html>
