/*
 * bootloader.c — Week 10 mini-project.
 *
 * A signed dual-bank bootloader for the RP2040. Lives at flash offset 0x100
 * (just after the SDK's 256-byte stage-1 boot2). Boots either the active or
 * staging application bank after Ed25519 signature verification; performs
 * staging->active swaps on request; rolls back on watchdog failure.
 *
 * Citations:
 *   - RP2040 datasheet §2.8 (Bootrom), pp. 130-141.
 *   - RFC 8032 §5.1 (Ed25519).
 *   - MCUboot design doc.
 *
 * Build: see CMakeLists.txt with CC_TARGET=bootloader.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#include "../exercises/bootloader_common.h"
#include "public_key.h"
#include "sha256.h"

/* -------------------------------------------------------------------------
 * External: Ed25519 verify entry point.
 * ----------------------------------------------------------------------- */

extern int ed25519_sign_open(const unsigned char *m,
                             size_t mlen,
                             const unsigned char *pk,
                             const unsigned char *RS);

/* -------------------------------------------------------------------------
 * Boot ROM flash API table.
 * ----------------------------------------------------------------------- */

typedef void (*flash_void_fn_t)(void);
typedef void (*flash_erase_fn_t)(uint32_t, size_t, uint32_t, uint8_t);
typedef void (*flash_program_fn_t)(uint32_t, const uint8_t *, size_t);
typedef void (*usb_boot_fn_t)(uint32_t, uint32_t);

typedef struct {
    flash_void_fn_t    connect_internal_flash;
    flash_void_fn_t    flash_exit_xip;
    flash_erase_fn_t   flash_range_erase;
    flash_program_fn_t flash_range_program;
    flash_void_fn_t    flash_flush_cache;
    flash_void_fn_t    flash_enter_cmd_xip;
    usb_boot_fn_t      reset_to_usb_boot;
} flash_api_t;

static flash_api_t g_api;

static void api_init(void) {
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
    g_api.reset_to_usb_boot =
        (usb_boot_fn_t)      rom_func_lookup_inline(rom_table_code('U', 'B'));
}

/* -------------------------------------------------------------------------
 * Flash write helper (SRAM-resident).
 * ----------------------------------------------------------------------- */

static void __not_in_flash_func(flash_safe_write)(uint32_t flash_offset,
                                                  const uint8_t *src,
                                                  size_t length) {
    uint32_t saved = save_and_disable_interrupts();

    g_api.connect_internal_flash();
    g_api.flash_exit_xip();
    g_api.flash_range_erase(flash_offset, length,
                            CC_FLASH_SECTOR_SIZE, CC_FLASH_ERASE_CMD);
    g_api.flash_range_program(flash_offset, src, length);
    g_api.flash_flush_cache();
    g_api.flash_enter_cmd_xip();

    restore_interrupts(saved);
}

static void __not_in_flash_func(flash_safe_erase)(uint32_t flash_offset,
                                                  size_t length) {
    uint32_t saved = save_and_disable_interrupts();

    g_api.connect_internal_flash();
    g_api.flash_exit_xip();
    g_api.flash_range_erase(flash_offset, length,
                            CC_FLASH_SECTOR_SIZE, CC_FLASH_ERASE_CMD);
    g_api.flash_flush_cache();
    g_api.flash_enter_cmd_xip();

    restore_interrupts(saved);
}

/* -------------------------------------------------------------------------
 * CRC32 (used for metadata-page integrity).
 * Same polynomial/algorithm as the Boot ROM uses for the boot2 CRC.
 * ----------------------------------------------------------------------- */

static uint32_t crc32_mpeg2(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < length; i++) {
        crc ^= ((uint32_t) data[i]) << 24;
        for (int j = 0; j < 8; j++) {
            if ((crc & 0x80000000u) != 0u) {
                crc = (crc << 1) ^ 0x04C11DB7u;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* -------------------------------------------------------------------------
 * Metadata read/write with ping-pong.
 * ----------------------------------------------------------------------- */

static int metadata_page_is_valid(const bootloader_metadata_t *m) {
    if (m->magic != BL_METADATA_MAGIC) return 0;

    uint32_t computed = crc32_mpeg2((const uint8_t *) m,
                                    BL_METADATA_STRUCT_SIZE - sizeof(uint32_t));
    if (computed != m->crc32) return 0;

    return 1;
}

static bl_result_t metadata_read(bootloader_metadata_t *out) {
    const bootloader_metadata_t *page_a =
        (const bootloader_metadata_t *) CC_METADATA_PAGE_A_ADDR;
    const bootloader_metadata_t *page_b =
        (const bootloader_metadata_t *) CC_METADATA_PAGE_B_ADDR;

    int a_valid = metadata_page_is_valid(page_a);
    int b_valid = metadata_page_is_valid(page_b);

    if (!a_valid && !b_valid) {
        return BL_ERR_NO_VALID_METADATA;
    }

    const bootloader_metadata_t *winner;
    if (a_valid && b_valid) {
        winner = (page_a->sequence > page_b->sequence) ? page_a : page_b;
    } else if (a_valid) {
        winner = page_a;
    } else {
        winner = page_b;
    }

    memcpy(out, winner, BL_METADATA_STRUCT_SIZE);
    return BL_OK;
}

static bl_result_t metadata_write(const bootloader_metadata_t *new_meta_in) {
    /* Determine which page is currently active so we write to the inactive
       one. Read both pages first. */
    const bootloader_metadata_t *page_a =
        (const bootloader_metadata_t *) CC_METADATA_PAGE_A_ADDR;
    const bootloader_metadata_t *page_b =
        (const bootloader_metadata_t *) CC_METADATA_PAGE_B_ADDR;

    int a_valid = metadata_page_is_valid(page_a);
    int b_valid = metadata_page_is_valid(page_b);

    uint32_t target_offset;
    if (!a_valid && !b_valid) {
        target_offset = CC_METADATA_PAGE_A_OFFSET;
    } else if (a_valid && !b_valid) {
        target_offset = CC_METADATA_PAGE_B_OFFSET;
    } else if (!a_valid && b_valid) {
        target_offset = CC_METADATA_PAGE_A_OFFSET;
    } else {
        target_offset = (page_a->sequence > page_b->sequence)
                          ? CC_METADATA_PAGE_B_OFFSET
                          : CC_METADATA_PAGE_A_OFFSET;
    }

    /* Build the page contents — the struct, padded to 4 KB with 0xFF. */
    static uint8_t page_buf[CC_METADATA_PAGE_SIZE];
    memset(page_buf, 0xFFu, sizeof(page_buf));

    bootloader_metadata_t to_write;
    memcpy(&to_write, new_meta_in, BL_METADATA_STRUCT_SIZE);
    to_write.magic = BL_METADATA_MAGIC;
    to_write.crc32 = crc32_mpeg2((const uint8_t *) &to_write,
                                 BL_METADATA_STRUCT_SIZE - sizeof(uint32_t));

    memcpy(page_buf, &to_write, BL_METADATA_STRUCT_SIZE);

    flash_safe_write(target_offset, page_buf, CC_METADATA_PAGE_SIZE);

    /* Verify. */
    const bootloader_metadata_t *check =
        (const bootloader_metadata_t *)(CC_FLASH_BASE + target_offset);
    if (!metadata_page_is_valid(check)) {
        return BL_ERR_VERIFY_MISMATCH;
    }
    if (check->sequence != to_write.sequence) {
        return BL_ERR_VERIFY_MISMATCH;
    }
    return BL_OK;
}

/* -------------------------------------------------------------------------
 * .ccf image verifier.
 * ----------------------------------------------------------------------- */

ccf_result_t cc_ccf_verify(const uint8_t *image,
                           size_t image_size,
                           const uint8_t public_key[CCF_PUBKEY_SIZE]) {
    if (image_size < CCF_MIN_FILE_SIZE) {
        return CCF_ERR_TOO_SHORT;
    }

    const ccf_header_t *hdr = (const ccf_header_t *) image;
    if (hdr->magic != CCF_MAGIC) {
        return CCF_ERR_BAD_MAGIC;
    }
    if (hdr->version != CCF_VERSION_1) {
        return CCF_ERR_BAD_VERSION;
    }
    if (hdr->fw_size > CCF_FW_SIZE_LIMIT) {
        return CCF_ERR_FW_TOO_BIG;
    }
    if ((size_t)(CCF_HEADER_SIZE + hdr->fw_size + CCF_SIGNATURE_SIZE)
            != image_size) {
        return CCF_ERR_BAD_SIZE;
    }

    /* Compute SHA-256 of header[0..15] + firmware bytes. The 32-byte sha256
       field in the header is excluded from the hash (chicken-and-egg). */
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, image, 16u);
    sha256_update(&ctx, image + CCF_HEADER_SIZE, hdr->fw_size);
    uint8_t computed_sha[CCF_SHA256_SIZE];
    sha256_finalize(&ctx, computed_sha);

    /* Constant-time compare. */
    uint8_t diff = 0u;
    for (size_t i = 0u; i < CCF_SHA256_SIZE; i++) {
        diff |= (uint8_t)(computed_sha[i] ^ hdr->fw_sha256[i]);
    }
    if (diff != 0u) {
        return CCF_ERR_SHA_MISMATCH;
    }

    /* Verify Ed25519 over header + firmware. */
    size_t signed_range = image_size - CCF_SIGNATURE_SIZE;
    int rv = ed25519_sign_open(image,
                               signed_range,
                               public_key,
                               image + signed_range);
    if (rv != 0) {
        return CCF_ERR_BAD_SIGNATURE;
    }

    return CCF_OK;
}

/* -------------------------------------------------------------------------
 * Bank verification helpers.
 * ----------------------------------------------------------------------- */

static int bank_image_verifies(uint32_t bank_addr, uint32_t fw_size) {
    /* The bank holds a .ccf image at offset 0 of the bank.
       Total .ccf size = 48 + fw_size + 64 bytes. */
    size_t image_size = (size_t)(CCF_HEADER_SIZE + fw_size + CCF_SIGNATURE_SIZE);
    if (image_size > CC_ACTIVE_BANK_SIZE) {
        return 0;
    }
    const uint8_t *image = (const uint8_t *) bank_addr;
    ccf_result_t r = cc_ccf_verify(image, image_size, bootloader_public_key);
    return (r == CCF_OK) ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * Swap: copy staging into active.
 * ----------------------------------------------------------------------- */

static bl_result_t do_swap(uint32_t fw_size) {
    /* Total .ccf size in staging bank. */
    size_t image_size = (size_t)(CCF_HEADER_SIZE + fw_size + CCF_SIGNATURE_SIZE);
    /* Round up to next 4 KB sector for the erase/program length. */
    size_t round_up = (image_size + CC_FLASH_SECTOR_SIZE - 1u)
                      & ~(size_t)(CC_FLASH_SECTOR_SIZE - 1u);
    if (round_up > CC_ACTIVE_BANK_SIZE) {
        return BL_ERR_IMAGE_TOO_BIG;
    }

    /* The active bank starts at offset CC_ACTIVE_BANK_OFFSET. We erase the
       portion we will overwrite, then program from the staging bank's bytes
       (which the XIP cache will fetch correctly because we are reading
       from one part of flash and programming a different part). */
    uint32_t saved = save_and_disable_interrupts();

    g_api.connect_internal_flash();
    g_api.flash_exit_xip();

    g_api.flash_range_erase(CC_ACTIVE_BANK_OFFSET,
                            round_up,
                            CC_FLASH_SECTOR_SIZE,
                            CC_FLASH_ERASE_CMD);

    /* Read staging directly from its XIP-mapped address, into a small
       RAM buffer, then program to active in chunks. We cannot read flash
       while XIP is disabled, so we re-enter XIP between reads. This is
       inefficient but correct for our 752 KB max copy. */
    g_api.flash_enter_cmd_xip();

    static uint8_t copy_buf[CC_FLASH_SECTOR_SIZE];
    for (uint32_t off = 0u; off < round_up; off += CC_FLASH_SECTOR_SIZE) {
        /* Read 4 KB from staging at off via XIP. */
        memcpy(copy_buf,
               (const uint8_t *)(CC_STAGING_BANK_ADDR + off),
               CC_FLASH_SECTOR_SIZE);

        /* Now program that 4 KB into active. */
        g_api.flash_exit_xip();
        g_api.flash_range_program(CC_ACTIVE_BANK_OFFSET + off,
                                  copy_buf,
                                  CC_FLASH_SECTOR_SIZE);
        g_api.flash_flush_cache();
        g_api.flash_enter_cmd_xip();
    }

    restore_interrupts(saved);

    return BL_OK;
}

/* -------------------------------------------------------------------------
 * Drop to USB MSC recovery via the Boot ROM.
 * ----------------------------------------------------------------------- */

static void __attribute__((noreturn)) drop_to_usb_msc(void) {
    g_api.reset_to_usb_boot(0u, 0u);
    for (;;) { __WFI(); }
}

/* -------------------------------------------------------------------------
 * Jump to the application.
 * ----------------------------------------------------------------------- */

static void __attribute__((noreturn))
boot_application(uint32_t app_vt_addr) {
    __disable_irq();

    const uint32_t *vt = (const uint32_t *) app_vt_addr;
    uint32_t app_sp = vt[0];
    uint32_t app_pc = vt[1];

    /* Sanity-check SP — must be in SRAM. */
    if (app_sp < 0x20000000u || app_sp > 0x20042000u) {
        drop_to_usb_msc();
    }
    /* Sanity-check PC — must be in flash with thumb bit set. */
    if ((app_pc & 0x10000000u) == 0u || (app_pc & 0x1u) == 0u) {
        drop_to_usb_msc();
    }

    /* Relocate VTOR. */
    *((volatile uint32_t *) 0xE000ED08u) = app_vt_addr;

    __DSB();
    __ISB();

    __set_MSP(app_sp);
    ((void (*)(void)) app_pc)();

    for (;;) { __WFI(); }
}

/* -------------------------------------------------------------------------
 * Bootloader main.
 * ----------------------------------------------------------------------- */

int main(void) {
    /* The bootloader has no stdio — too much code. We use raw GPIO 25
       (Pico's LED) as a status indicator. */
    api_init();

    /* Initialize the LED via raw SIO without the SDK helpers. */
    *((volatile uint32_t *) 0x40014000u + (25u * 2u)) = 5u; /* GPIO 25 = SIO */
    *((volatile uint32_t *) 0xD0000024u) = (1u << 25);       /* OE set */

    /* Read metadata. */
    bootloader_metadata_t meta;
    bl_result_t mr = metadata_read(&meta);

    if (mr == BL_ERR_NO_VALID_METADATA) {
        /* First boot ever — initialize metadata. */
        memset(&meta, 0, sizeof(meta));
        meta.magic         = BL_METADATA_MAGIC;
        meta.sequence      = 1u;
        meta.state         = BL_STATE_IDLE;
        meta.active_addr   = CC_ACTIVE_BANK_ADDR;
        meta.staging_addr  = CC_STAGING_BANK_ADDR;
        meta.boot_confirmed = 1u;
        meta.boot_attempts = 0u;
        (void) metadata_write(&meta);
    }

    /* Inspect watchdog scratch4 for a "drop to USB" request. */
    if (watchdog_hw->scratch[4] == BL_RESET_MAGIC_USB_MSC) {
        watchdog_hw->scratch[4] = 0u;
        drop_to_usb_msc();
    }

    /* Inspect watchdog scratch4 for a "swap requested" magic. */
    int swap_requested =
        (watchdog_hw->scratch[4] == BL_RESET_MAGIC_SWAP);
    if (swap_requested) {
        watchdog_hw->scratch[4] = 0u;
        meta.state = BL_STATE_SWAP_REQUESTED;
    }

    /* If a swap is pending in metadata, verify staging and perform copy. */
    if (meta.state == BL_STATE_SWAP_REQUESTED ||
        meta.state == BL_STATE_SWAP_IN_PROGRESS) {

        if (bank_image_verifies(CC_STAGING_BANK_ADDR, meta.staging_size)) {
            /* Mark swap as in-progress, then copy, then mark booting-new. */
            meta.state    = BL_STATE_SWAP_IN_PROGRESS;
            meta.sequence += 1u;
            (void) metadata_write(&meta);

            if (do_swap(meta.staging_size) == BL_OK) {
                meta.state           = BL_STATE_BOOTING_NEW;
                meta.boot_confirmed  = 0u;
                meta.boot_attempts   = 1u;
                meta.sequence       += 1u;
                (void) metadata_write(&meta);
            } else {
                /* Swap failed — fall through to attempt boot of whatever
                   is in active. */
                meta.state = BL_STATE_IDLE;
                (void) metadata_write(&meta);
            }
        } else {
            /* Staging signature invalid — give up on the swap. */
            meta.state = BL_STATE_IDLE;
            meta.sequence += 1u;
            (void) metadata_write(&meta);
        }
    }

    /* If we are in BOOTING_NEW and the previous boot did not confirm,
       check attempts and possibly roll back. */
    if (meta.state == BL_STATE_BOOTING_NEW && meta.boot_confirmed == 0u) {
        if (meta.boot_attempts >= 3u) {
            /* Roll back: copy staging (which holds the previous good firmware
               since we did not erase it after the last swap) into active. */
            if (bank_image_verifies(CC_STAGING_BANK_ADDR, meta.staging_size)) {
                meta.state         = BL_STATE_SWAP_IN_PROGRESS;
                meta.sequence     += 1u;
                (void) metadata_write(&meta);

                if (do_swap(meta.staging_size) == BL_OK) {
                    meta.state          = BL_STATE_IDLE;
                    meta.boot_confirmed = 1u;
                    meta.boot_attempts  = 0u;
                    meta.sequence      += 1u;
                    (void) metadata_write(&meta);
                }
            } else {
                /* Cannot roll back — staging is corrupt. Drop to recovery. */
                drop_to_usb_msc();
            }
        } else {
            meta.boot_attempts += 1u;
            meta.sequence      += 1u;
            (void) metadata_write(&meta);
        }
    }

    /* Verify active and boot. */
    if (!bank_image_verifies(CC_ACTIVE_BANK_ADDR, meta.staging_size)) {
        /* Active is corrupt — try staging as a last resort. */
        if (bank_image_verifies(CC_STAGING_BANK_ADDR, meta.staging_size)) {
            (void) do_swap(meta.staging_size);
            /* Fall through and retry boot of active. */
            if (!bank_image_verifies(CC_ACTIVE_BANK_ADDR, meta.staging_size)) {
                drop_to_usb_msc();
            }
        } else {
            drop_to_usb_msc();
        }
    }

    /* Application's vector table lives at active bank + 48 (past the
       .ccf header). */
    boot_application(CC_ACTIVE_BANK_ADDR + CCF_HEADER_SIZE);
}

/* End of bootloader.c. */
