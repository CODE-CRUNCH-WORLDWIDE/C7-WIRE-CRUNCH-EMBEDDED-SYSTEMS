/*
 * application.c — Week 10 mini-project, application side.
 *
 * Runs from the active bank at 0x10008100. Exposes a USB CDC virtual serial
 * port. Implements the OTA receive protocol described in Lecture 3.
 *
 * The OTA receive flow:
 *   Host -> Device: BEGIN <size> <sha256>\n
 *   Device -> Host: OK\n (after erasing the staging bank)
 *   Host -> Device: CHUNK <offset> <length> <hex>\n (repeated)
 *   Device -> Host: OK <offset>\n (per chunk)
 *   Host -> Device: END\n
 *   Device -> Host: OK\n (after verifying overall SHA)
 *   Host -> Device: REBOOT\n
 *   Device -> Host: OK\n (then triggers reset)
 *
 * Citations:
 *   - RP2040 datasheet §2.8.3 (Bootrom API).
 *   - MCUboot serial-recovery protocol (the design inspiration).
 *   - RFC 6234 (SHA-256).
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "tusb.h"

#include "../exercises/bootloader_common.h"
#include "sha256.h"

/* -------------------------------------------------------------------------
 * Boot ROM flash API (same as bootloader's — copied so application is
 * standalone).
 * ----------------------------------------------------------------------- */

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
}

static void __not_in_flash_func(flash_safe_write)(uint32_t flash_offset,
                                                  const uint8_t *src,
                                                  size_t length) {
    uint32_t saved = save_and_disable_interrupts();

    g_api.connect_internal_flash();
    g_api.flash_exit_xip();
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
 * Minimal metadata access (read-only from application; writes happen at
 * confirmation and at swap-request time).
 * ----------------------------------------------------------------------- */

static uint32_t app_crc32_mpeg2(const uint8_t *data, size_t length) {
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

static int app_metadata_valid(const bootloader_metadata_t *m) {
    if (m->magic != BL_METADATA_MAGIC) return 0;
    uint32_t c = app_crc32_mpeg2((const uint8_t *) m,
                                 BL_METADATA_STRUCT_SIZE - sizeof(uint32_t));
    return (c == m->crc32) ? 1 : 0;
}

static int app_metadata_read(bootloader_metadata_t *out) {
    const bootloader_metadata_t *a =
        (const bootloader_metadata_t *) CC_METADATA_PAGE_A_ADDR;
    const bootloader_metadata_t *b =
        (const bootloader_metadata_t *) CC_METADATA_PAGE_B_ADDR;
    int va = app_metadata_valid(a);
    int vb = app_metadata_valid(b);
    if (!va && !vb) return -1;
    const bootloader_metadata_t *w;
    if (va && vb) w = (a->sequence > b->sequence) ? a : b;
    else if (va)  w = a;
    else          w = b;
    memcpy(out, w, BL_METADATA_STRUCT_SIZE);
    return 0;
}

static int app_metadata_write(const bootloader_metadata_t *m) {
    const bootloader_metadata_t *a =
        (const bootloader_metadata_t *) CC_METADATA_PAGE_A_ADDR;
    const bootloader_metadata_t *b =
        (const bootloader_metadata_t *) CC_METADATA_PAGE_B_ADDR;
    int va = app_metadata_valid(a);
    int vb = app_metadata_valid(b);

    uint32_t target_offset;
    if (!va && !vb) target_offset = CC_METADATA_PAGE_A_OFFSET;
    else if (va && !vb) target_offset = CC_METADATA_PAGE_B_OFFSET;
    else if (!va && vb) target_offset = CC_METADATA_PAGE_A_OFFSET;
    else target_offset = (a->sequence > b->sequence)
                            ? CC_METADATA_PAGE_B_OFFSET
                            : CC_METADATA_PAGE_A_OFFSET;

    static uint8_t page_buf[CC_METADATA_PAGE_SIZE];
    memset(page_buf, 0xFFu, sizeof(page_buf));

    bootloader_metadata_t to_write;
    memcpy(&to_write, m, BL_METADATA_STRUCT_SIZE);
    to_write.magic = BL_METADATA_MAGIC;
    to_write.crc32 = app_crc32_mpeg2((const uint8_t *) &to_write,
                                     BL_METADATA_STRUCT_SIZE
                                     - sizeof(uint32_t));
    memcpy(page_buf, &to_write, BL_METADATA_STRUCT_SIZE);

    flash_safe_erase(target_offset, CC_METADATA_PAGE_SIZE);
    flash_safe_write(target_offset, page_buf, CC_METADATA_PAGE_SIZE);
    return 0;
}

/* -------------------------------------------------------------------------
 * Smoke-test and confirm-boot.
 * ----------------------------------------------------------------------- */

static bool smoke_test_passed(void) {
    /* Real applications would probe their peripherals here. For this
       reference we declare success unconditionally. */
    return true;
}

static void confirm_boot_if_needed(void) {
    bootloader_metadata_t meta;
    if (app_metadata_read(&meta) != 0) return;

    if (meta.state == BL_STATE_BOOTING_NEW) {
        if (!smoke_test_passed()) {
            /* Force a reset; bootloader will increment boot_attempts. */
            watchdog_reboot(0, 0, 0);
            for (;;) { }
        }
        meta.state          = BL_STATE_IDLE;
        meta.boot_confirmed = 1u;
        meta.boot_attempts  = 0u;
        meta.sequence      += 1u;
        (void) app_metadata_write(&meta);
    }
}

/* -------------------------------------------------------------------------
 * OTA protocol.
 * ----------------------------------------------------------------------- */

typedef enum {
    OTA_IDLE      = 0,
    OTA_RECEIVING = 1,
    OTA_FINALIZED = 2,
    OTA_ERROR     = 3,
} ota_state_t;

typedef struct {
    ota_state_t   state;
    uint32_t      total_size;
    uint8_t       expected_sha[32];
    uint32_t      bytes_received;
    sha256_ctx_t  running_sha;
    uint32_t      pending_flash_offset;
} ota_session_t;

static ota_session_t g_ota;

static void ota_send_line(const char *msg) {
    while (*msg != '\0') {
        tud_cdc_write_char(*msg);
        msg++;
    }
    tud_cdc_write_char('\n');
    tud_cdc_write_flush();
}

static void ota_send_err(int code, const char *msg) {
    char buf[80];
    snprintf(buf, sizeof(buf), "ERR %d %s", code, msg);
    ota_send_line(buf);
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int hex_decode(const char *s, size_t hex_len, uint8_t *out) {
    if ((hex_len & 1u) != 0u) return -1;
    for (size_t i = 0u; i < hex_len; i += 2u) {
        int hi = hex_nibble(s[i]);
        int lo = hex_nibble(s[i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i / 2u] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static void ota_handle_begin(const char *args) {
    /* args: "<size_decimal> <sha256_hex>" */
    char *end = NULL;
    unsigned long size = strtoul(args, &end, 10);
    if (end == args || *end != ' ') {
        ota_send_err(1, "parse-size");
        return;
    }
    if (size > CCF_FW_SIZE_LIMIT + CCF_MIN_FILE_SIZE) {
        ota_send_err(2, "too-big");
        return;
    }

    /* Skip space, parse hex. */
    const char *hex = end + 1;
    size_t hex_len = strlen(hex);
    if (hex_len < 64u) {
        ota_send_err(3, "sha-short");
        return;
    }
    if (hex_decode(hex, 64u, g_ota.expected_sha) != 0) {
        ota_send_err(4, "sha-hex");
        return;
    }

    /* Erase the entire staging bank. This is the slow part (~8 seconds). */
    flash_safe_erase(CC_STAGING_BANK_OFFSET, CC_STAGING_BANK_SIZE);

    g_ota.state          = OTA_RECEIVING;
    g_ota.total_size     = (uint32_t) size;
    g_ota.bytes_received = 0u;
    sha256_init(&g_ota.running_sha);
    g_ota.pending_flash_offset = 0u;

    ota_send_line("OK");
}

static void ota_handle_chunk(const char *args) {
    if (g_ota.state != OTA_RECEIVING) {
        ota_send_err(10, "not-receiving");
        return;
    }

    char *end = NULL;
    unsigned long offset = strtoul(args, &end, 10);
    if (end == args || *end != ' ') { ota_send_err(11, "parse-offset"); return; }

    const char *p = end + 1;
    unsigned long length = strtoul(p, &end, 10);
    if (end == p || *end != ' ') { ota_send_err(12, "parse-length"); return; }
    if (length == 0u || length > 256u) { ota_send_err(13, "bad-length"); return; }

    const char *hex = end + 1;
    if (strlen(hex) < length * 2u) { ota_send_err(14, "hex-short"); return; }

    if ((uint32_t) offset != g_ota.bytes_received) {
        ota_send_err(15, "offset-skew");
        return;
    }

    uint8_t buf[CC_FLASH_PAGE_SIZE];
    memset(buf, 0xFFu, sizeof(buf));
    if (hex_decode(hex, length * 2u, buf) != 0) {
        ota_send_err(16, "hex-decode");
        return;
    }

    /* Write a full page (256 bytes); short chunks padded with 0xFF. */
    flash_safe_write(CC_STAGING_BANK_OFFSET + offset, buf, CC_FLASH_PAGE_SIZE);

    /* Update running SHA over only the valid bytes. */
    sha256_update(&g_ota.running_sha, buf, length);
    g_ota.bytes_received += (uint32_t) length;

    char rsp[32];
    snprintf(rsp, sizeof(rsp), "OK %lu", offset);
    ota_send_line(rsp);
}

static void ota_handle_end(void) {
    if (g_ota.state != OTA_RECEIVING) {
        ota_send_err(20, "not-receiving");
        return;
    }
    if (g_ota.bytes_received != g_ota.total_size) {
        ota_send_err(21, "size-mismatch");
        g_ota.state = OTA_ERROR;
        return;
    }

    uint8_t computed[32];
    sha256_finalize(&g_ota.running_sha, computed);
    uint8_t diff = 0u;
    for (size_t i = 0u; i < 32u; i++) {
        diff |= (uint8_t)(computed[i] ^ g_ota.expected_sha[i]);
    }
    if (diff != 0u) {
        ota_send_err(22, "sha-mismatch");
        g_ota.state = OTA_ERROR;
        return;
    }

    /* Write metadata to mark swap-requested. The total fw_size in metadata
       is total_size minus the .ccf header (48) and signature (64). */
    bootloader_metadata_t meta;
    if (app_metadata_read(&meta) == 0) {
        meta.state         = BL_STATE_SWAP_REQUESTED;
        meta.staging_size  = g_ota.total_size - CCF_HEADER_SIZE - CCF_SIGNATURE_SIZE;
        memcpy(meta.staging_sha256, g_ota.expected_sha, 32u);
        meta.sequence     += 1u;
        (void) app_metadata_write(&meta);
    }

    g_ota.state = OTA_FINALIZED;
    ota_send_line("OK");
}

static void ota_handle_reboot(void) {
    if (g_ota.state != OTA_FINALIZED) {
        ota_send_err(30, "not-finalized");
        return;
    }

    /* Signal the bootloader via watchdog scratch4 then reset. */
    watchdog_hw->scratch[4] = BL_RESET_MAGIC_SWAP;
    ota_send_line("OK");
    sleep_ms(100);  /* let the OK byte make it to the host. */
    watchdog_reboot(0, 0, 1);  /* 1 ms timeout */
    for (;;) { __WFI(); }
}

static void ota_handle_line(char *line) {
    /* Trim trailing whitespace. */
    size_t n = strlen(line);
    while (n > 0u && (line[n - 1] == '\r' || line[n - 1] == '\n' ||
                       line[n - 1] == ' ')) {
        line[--n] = '\0';
    }
    if (n == 0u) return;

    if (strncmp(line, "BEGIN ", 6u) == 0) {
        ota_handle_begin(line + 6u);
    } else if (strncmp(line, "CHUNK ", 6u) == 0) {
        ota_handle_chunk(line + 6u);
    } else if (strcmp(line, "END") == 0) {
        ota_handle_end();
    } else if (strcmp(line, "REBOOT") == 0) {
        ota_handle_reboot();
    } else {
        ota_send_err(99, "unknown");
    }
}

/* -------------------------------------------------------------------------
 * Line buffer for incoming CDC data.
 * ----------------------------------------------------------------------- */

#define LINE_BUF_SIZE 1200u
static char  g_line_buf[LINE_BUF_SIZE];
static size_t g_line_len = 0u;

static void cdc_pump(void) {
    while (tud_cdc_available()) {
        char c = (char) tud_cdc_read_char();
        if (c == '\n') {
            g_line_buf[g_line_len] = '\0';
            ota_handle_line(g_line_buf);
            g_line_len = 0u;
        } else if (g_line_len < LINE_BUF_SIZE - 1u) {
            g_line_buf[g_line_len++] = c;
        } else {
            /* Line too long; reset. */
            g_line_len = 0u;
            ota_send_err(98, "line-overflow");
        }
    }
}

/* -------------------------------------------------------------------------
 * Application main.
 * ----------------------------------------------------------------------- */

int main(void) {
    stdio_init_all();
    api_init();

    /* Initialize TinyUSB. */
    tusb_init();

    sleep_ms(1500);
    confirm_boot_if_needed();

    /* Initial diagnostic line over CDC. */
    printf("[app] OTA receiver ready.\n");

    while (true) {
        tud_task();
        if (tud_cdc_connected()) {
            cdc_pump();
        }
    }
    return 0;
}

/* End of application.c. */
