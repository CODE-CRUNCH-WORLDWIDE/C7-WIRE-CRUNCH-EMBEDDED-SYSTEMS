/*
 * bootloader_common.h — Shared declarations for Week 10 exercises and mini-project.
 *
 * Constants here mirror the flash layout described in Lecture 2 and the
 * signed-image format described in Lecture 3. Citations:
 *   - RP2040 datasheet §2.8 (Bootrom), pp. 130-141.
 *   - Microsoft UF2 spec, https://github.com/microsoft/uf2#file-format.
 *   - RFC 8032 §5.1 (Ed25519 algorithm).
 *   - Winbond W25Q16JV §7.2.10 (Page Program), §7.2.18 (Sector Erase).
 */

#ifndef CC_BOOTLOADER_COMMON_H_
#define CC_BOOTLOADER_COMMON_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Flash layout (memory-mapped addresses).
 *
 * Total flash:           2 MB at 0x10000000 - 0x101FFFFF.
 * Stage 1 bootloader:    0x10000000 + 256 bytes.
 * Bootloader:            0x10000100 + 32 KB.
 * Active app bank:       0x10008100 + ~752 KB.
 * Staging app bank:      0x100C4100 + ~752 KB.
 * Metadata pages:        0x101FC000, 0x101FD000 (4 KB each).
 * ----------------------------------------------------------------------- */

#define CC_FLASH_BASE                 0x10000000u
#define CC_FLASH_SIZE_BYTES           0x00200000u  /* 2 MB */

#define CC_BOOT2_OFFSET               0x00000000u
#define CC_BOOT2_SIZE                 256u

#define CC_BOOTLOADER_OFFSET          0x00000100u
#define CC_BOOTLOADER_SIZE            (32u * 1024u)
#define CC_BOOTLOADER_ADDR            (CC_FLASH_BASE + CC_BOOTLOADER_OFFSET)

#define CC_ACTIVE_BANK_OFFSET         0x00008100u
#define CC_ACTIVE_BANK_SIZE           770304u  /* ~752 KB */
#define CC_ACTIVE_BANK_ADDR           (CC_FLASH_BASE + CC_ACTIVE_BANK_OFFSET)

#define CC_STAGING_BANK_OFFSET        0x000C4100u
#define CC_STAGING_BANK_SIZE          770304u  /* ~752 KB */
#define CC_STAGING_BANK_ADDR          (CC_FLASH_BASE + CC_STAGING_BANK_OFFSET)

#define CC_METADATA_PAGE_A_OFFSET     0x001FC000u
#define CC_METADATA_PAGE_B_OFFSET     0x001FD000u
#define CC_METADATA_PAGE_A_ADDR       (CC_FLASH_BASE + CC_METADATA_PAGE_A_OFFSET)
#define CC_METADATA_PAGE_B_ADDR       (CC_FLASH_BASE + CC_METADATA_PAGE_B_OFFSET)
#define CC_METADATA_PAGE_SIZE         4096u

/* Flash hardware constants (Winbond W25Q16JV). */
#define CC_FLASH_SECTOR_SIZE          4096u
#define CC_FLASH_PAGE_SIZE            256u
#define CC_FLASH_ERASE_CMD            0x20u  /* 4 KB sector erase opcode. */

/* -------------------------------------------------------------------------
 * UF2 format constants (Microsoft UF2 spec).
 * ----------------------------------------------------------------------- */

#define UF2_BLOCK_SIZE                512u
#define UF2_HEADER_SIZE               32u
#define UF2_MAX_PAYLOAD               476u
#define UF2_RP2040_PAYLOAD            256u

#define UF2_MAGIC_START0_VALUE        0x0A324655u
#define UF2_MAGIC_START1_VALUE        0x9E5D5157u
#define UF2_MAGIC_END_VALUE           0x0AB16F30u

#define UF2_FLAG_NOT_MAIN_FLASH       0x00000001u
#define UF2_FLAG_FILE_CONTAINER       0x00001000u
#define UF2_FLAG_FAMILYID_PRESENT     0x00002000u
#define UF2_FLAG_MD5_PRESENT          0x00004000u
#define UF2_FLAG_EXTENSION_TAGS       0x00008000u

#define UF2_FAMILY_RP2040             0xE48BFF56u

typedef enum {
    UF2_OK                       = 0,
    UF2_ERR_BAD_MAGIC0           = 1,
    UF2_ERR_BAD_MAGIC1           = 2,
    UF2_ERR_BAD_MAGIC_END        = 3,
    UF2_ERR_PAYLOAD_TOO_BIG      = 4,
    UF2_ERR_NO_FAMILY            = 5,
    UF2_ERR_WRONG_FAMILY         = 6,
    UF2_ERR_BAD_BLOCK_NUMBER     = 7,
} uf2_result_t;

typedef struct {
    uint32_t target_addr;
    uint32_t length;
    const uint8_t *data;
} uf2_block_payload_t;

/* -------------------------------------------------------------------------
 * Signed-image (.ccf) format constants.
 * ----------------------------------------------------------------------- */

#define CCF_MAGIC                     0x43434631u   /* 'CCF1' little-endian. */
#define CCF_VERSION_1                 1u
#define CCF_HEADER_SIZE               48u
#define CCF_SIGNATURE_SIZE            64u
#define CCF_MIN_FILE_SIZE             (CCF_HEADER_SIZE + CCF_SIGNATURE_SIZE)
#define CCF_FW_SIZE_LIMIT             CC_STAGING_BANK_SIZE
#define CCF_PUBKEY_SIZE               32u
#define CCF_SHA256_SIZE               32u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t fw_size;
    uint32_t flags;
    uint8_t  fw_sha256[CCF_SHA256_SIZE];
    /* fw_size bytes of firmware follow. */
    /* 64 bytes of Ed25519 signature follow the firmware. */
} ccf_header_t;

typedef enum {
    CCF_OK                       = 0,
    CCF_ERR_TOO_SHORT            = 1,
    CCF_ERR_BAD_MAGIC            = 2,
    CCF_ERR_BAD_VERSION          = 3,
    CCF_ERR_BAD_SIZE             = 4,
    CCF_ERR_FW_TOO_BIG           = 5,
    CCF_ERR_SHA_MISMATCH         = 6,
    CCF_ERR_BAD_SIGNATURE        = 7,
} ccf_result_t;

/* -------------------------------------------------------------------------
 * Bootloader metadata (lives in metadata page A or B).
 * ----------------------------------------------------------------------- */

#define BL_METADATA_MAGIC             0xCCC10BEEu

typedef enum {
    BL_STATE_IDLE              = 0u,
    BL_STATE_SWAP_REQUESTED    = 1u,
    BL_STATE_SWAP_IN_PROGRESS  = 2u,
    BL_STATE_BOOTING_NEW       = 3u,
    BL_STATE_ROLLBACK_PENDING  = 4u,
} bl_state_t;

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint32_t state;            /* bl_state_t */
    uint32_t active_addr;
    uint32_t staging_addr;
    uint32_t staging_size;
    uint8_t  staging_sha256[CCF_SHA256_SIZE];
    uint8_t  staging_signature[CCF_SIGNATURE_SIZE];
    uint32_t boot_confirmed;
    uint32_t boot_attempts;
    uint32_t crc32;
} bootloader_metadata_t;

#define BL_METADATA_STRUCT_SIZE  (sizeof(bootloader_metadata_t))

/* Reset cause codes carried in watchdog scratch4. */
#define BL_RESET_MAGIC_SWAP           0xCCC10A55u  /* "CC10 AS5"   — swap requested. */
#define BL_RESET_MAGIC_USB_MSC        0xB007C0D3u  /* "BOOT CODE" — to Boot ROM USB MSC. */

/* Result codes for bootloader operations. */
typedef enum {
    BL_OK                        = 0,
    BL_ERR_NO_VALID_METADATA     = 1,
    BL_ERR_FLASH_ERASE_FAILED    = 2,
    BL_ERR_FLASH_PROGRAM_FAILED  = 3,
    BL_ERR_VERIFY_MISMATCH       = 4,
    BL_ERR_SIGNATURE_INVALID     = 5,
    BL_ERR_SHA_MISMATCH          = 6,
    BL_ERR_IMAGE_TOO_BIG         = 7,
    BL_ERR_IMAGE_BAD_FORMAT      = 8,
} bl_result_t;

/* -------------------------------------------------------------------------
 * Helper: little-endian load.
 * ----------------------------------------------------------------------- */

static inline uint32_t cc_read_le32(const uint8_t *p) {
    /* Compiler typically folds these four loads into one LDR on Cortex-M0+,
       but the code is written portably so the host-side build (where we run
       Exercise 1 on the laptop, not on the device) sees the same semantics. */
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline void cc_write_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v       & 0xFFu);
    p[1] = (uint8_t)((v >> 8 ) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/* -------------------------------------------------------------------------
 * UF2 parser API (implemented in exercise-01-uf2-parser.c).
 * ----------------------------------------------------------------------- */

uf2_result_t cc_uf2_parse_block(const uint8_t *block, uf2_block_payload_t *out);

/* -------------------------------------------------------------------------
 * Signed-image verifier API (implemented in exercise-03-ed25519-verify.c).
 * ----------------------------------------------------------------------- */

ccf_result_t cc_ccf_verify(const uint8_t *image,
                           size_t image_size,
                           const uint8_t public_key[CCF_PUBKEY_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* CC_BOOTLOADER_COMMON_H_ */
