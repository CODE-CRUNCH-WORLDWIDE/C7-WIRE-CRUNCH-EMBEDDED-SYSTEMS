/*
 * exercise-02-flash-write-primitives.c — Erase + program a 4 KB sector.
 *
 * This exercise targets the RP2040 on the bench. It demonstrates the Boot ROM
 * flash API: lookup function pointers from the ROM table, mask interrupts,
 * leave XIP, erase one 4 KB sector, program 16 256-byte pages, flush the XIP
 * cache, re-enter XIP, restore interrupts. Then it reads back the sector and
 * verifies the contents byte-for-byte.
 *
 * Builds with the Pico SDK:
 *   add_executable(exercise2 exercise-02-flash-write-primitives.c)
 *   target_link_libraries(exercise2 pico_stdlib pico_bootrom hardware_flash hardware_sync)
 *   pico_add_extra_outputs(exercise2)
 *
 * The target sector is at flash offset 0x000FF000 — well clear of our
 * application's text segment (which lives below 0x00040000 in this build).
 * Picking an address inside .text would corrupt the running program; picking
 * an address inside the boot2 region would brick the chip.
 *
 * Citations:
 *   - RP2040 datasheet §2.8.3 (Bootrom API), pp. 132-135.
 *   - Winbond W25Q16JV §7.2.10 (Page Program), §7.2.18 (Sector Erase).
 *   - Pico SDK src/rp2_common/hardware_flash/flash.c.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/sync.h"

#include "bootloader_common.h"

/* The flash offset we will exercise — must be outside any code/data section. */
#define TARGET_FLASH_OFFSET   0x000FF000u
#define TARGET_FLASH_LENGTH   CC_FLASH_SECTOR_SIZE  /* one 4 KB sector */

/* Cached Boot ROM function pointers. */
typedef void (*flash_void_fn_t)(void);
typedef void (*flash_erase_fn_t)(uint32_t, size_t, uint32_t, uint8_t);
typedef void (*flash_program_fn_t)(uint32_t, const uint8_t *, size_t);

typedef struct {
    flash_void_fn_t    connect_internal_flash;
    flash_void_fn_t    flash_exit_xip;
    flash_erase_fn_t   flash_range_erase;
    flash_program_fn_t flash_range_program;
    flash_void_fn_t    flash_flush_cache;
    flash_void_fn_t    flash_enter_cmd_xip;
} flash_api_t;

static flash_api_t g_api;

static void flash_api_init(void) {
    g_api.connect_internal_flash =
        (flash_void_fn_t)    rom_func_lookup_inline(rom_table_code('I', 'F'));
    g_api.flash_exit_xip =
        (flash_void_fn_t)    rom_func_lookup_inline(rom_table_code('E', 'X'));
    g_api.flash_range_erase =
        (flash_erase_fn_t)   rom_func_lookup_inline(rom_table_code('R', 'E'));
    g_api.flash_range_program =
        (flash_program_fn_t) rom_func_lookup_inline(rom_table_code('R', 'P'));
    g_api.flash_flush_cache =
        (flash_void_fn_t)    rom_func_lookup_inline(rom_table_code('F', 'C'));
    g_api.flash_enter_cmd_xip =
        (flash_void_fn_t)    rom_func_lookup_inline(rom_table_code('C', 'X'));
}

/*
 * The flash write itself. Marked __not_in_flash_func because the entire body
 * (including the function epilogue) must execute from SRAM — XIP is disabled
 * for the duration.
 */
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

/*
 * Fill the buffer with a pattern that exercises the page-program path.
 * Each 256-byte page gets a different fill so we can tell which page failed
 * if verification fails.
 */
static void make_test_pattern(uint8_t *buf, size_t length) {
    for (size_t i = 0u; i < length; i++) {
        size_t page_index = i / CC_FLASH_PAGE_SIZE;
        size_t in_page    = i % CC_FLASH_PAGE_SIZE;
        buf[i] = (uint8_t)(((page_index * 17u) + in_page) & 0xFFu);
    }
}

/*
 * Verify the on-flash bytes by reading through the XIP region.
 * Returns the number of mismatches; 0 means success.
 */
static uint32_t verify_flash(uint32_t flash_offset,
                             const uint8_t *expected,
                             size_t length) {
    const uint8_t *xip_view = (const uint8_t *)(CC_FLASH_BASE + flash_offset);
    uint32_t mismatches = 0u;
    for (size_t i = 0u; i < length; i++) {
        if (xip_view[i] != expected[i]) {
            mismatches++;
            if (mismatches <= 8u) {
                printf("  mismatch at +%zu: got 0x%02X expected 0x%02X\n",
                       i, xip_view[i], expected[i]);
            }
        }
    }
    return mismatches;
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);  /* wait for CDC enumeration */

    printf("\n=== Exercise 2: Flash write primitives ===\n");
    printf("Target flash offset: 0x%08" PRIx32 "\n", (uint32_t) TARGET_FLASH_OFFSET);
    printf("Target memory addr:  0x%08" PRIx32 "\n",
           (uint32_t)(CC_FLASH_BASE + TARGET_FLASH_OFFSET));
    printf("Length:              %u bytes (one 4 KB sector)\n",
           (unsigned) TARGET_FLASH_LENGTH);
    printf("\n");

    flash_api_init();
    printf("Boot ROM function pointers cached.\n");

    /* Pattern buffer in SRAM. */
    static uint8_t pattern[TARGET_FLASH_LENGTH];
    make_test_pattern(pattern, TARGET_FLASH_LENGTH);
    printf("Test pattern generated: pattern[0]=0x%02X pattern[255]=0x%02X "
           "pattern[256]=0x%02X pattern[4095]=0x%02X\n",
           pattern[0], pattern[255], pattern[256], pattern[4095]);

    /* Measure the write. */
    absolute_time_t t0 = get_absolute_time();
    do_flash_write(TARGET_FLASH_OFFSET, pattern, TARGET_FLASH_LENGTH);
    absolute_time_t t1 = get_absolute_time();
    int64_t elapsed_us = absolute_time_diff_us(t0, t1);

    printf("Write complete in %" PRId64 " us "
           "(~%.2f ms erase + %.2f ms program estimated).\n",
           elapsed_us,
           45.0,
           (16.0 * 0.4));

    /* Verify. */
    uint32_t bad = verify_flash(TARGET_FLASH_OFFSET, pattern, TARGET_FLASH_LENGTH);
    if (bad == 0u) {
        printf("\nVERIFY PASS: %u bytes match.\n", (unsigned) TARGET_FLASH_LENGTH);
    } else {
        printf("\nVERIFY FAIL: %" PRIu32 " mismatched bytes.\n", bad);
    }

    /* Throughput. */
    double throughput_kb_s =
        ((double) TARGET_FLASH_LENGTH / 1024.0) /
        ((double) elapsed_us / 1000000.0);
    printf("Effective throughput: %.1f KB/s.\n", throughput_kb_s);

    /* Idle loop. The BOOTSEL button still works for recovery. */
    while (true) {
        sleep_ms(1000);
    }
    return 0;
}

/* End of exercise-02-flash-write-primitives.c. */
