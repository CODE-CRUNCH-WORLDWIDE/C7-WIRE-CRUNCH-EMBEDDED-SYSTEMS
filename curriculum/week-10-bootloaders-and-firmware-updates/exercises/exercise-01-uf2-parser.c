/*
 * exercise-01-uf2-parser.c — Parse a UF2 file into target-addr/payload pairs.
 *
 * The UF2 file format is Microsoft's drag-and-drop firmware format. Spec at
 * https://github.com/microsoft/uf2#file-format. We build a parser that:
 *
 *   1. Validates the three magic numbers and the family ID.
 *   2. Extracts target_addr, payload, and payload length from each 512-byte block.
 *   3. Verifies block sequencing (block N has blockNo == N for N in [0, numBlocks-1]).
 *   4. Prints a summary table to stdout.
 *
 * This exercise is intended to run on the developer's laptop, not on the Pico.
 * It reads a UF2 file from argv[1] and prints what the bootloader would do
 * if it consumed the file.
 *
 * Build:
 *   cc -std=c11 -Wall -Wextra -O2 -o uf2parse exercise-01-uf2-parser.c
 *
 * Citations:
 *   - UF2 spec, https://github.com/microsoft/uf2#file-format
 *   - RP2040 datasheet §2.8.5 (USB MSC mode), pp. 138-141.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include "bootloader_common.h"

/* -------------------------------------------------------------------------
 * UF2 block parser. Operates on a single 512-byte block.
 * ----------------------------------------------------------------------- */

uf2_result_t cc_uf2_parse_block(const uint8_t *block, uf2_block_payload_t *out) {
    if (block == NULL || out == NULL) {
        return UF2_ERR_BAD_BLOCK_NUMBER;
    }

    /* Read header fields with explicit little-endian loads — the file is
       on-disk format, not the host's native byte order. */
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

    /* The payload size must fit in the 476-byte data area. */
    if (payload_size > UF2_MAX_PAYLOAD) return UF2_ERR_PAYLOAD_TOO_BIG;

    /* For RP2040, the family ID flag must be set and the value must match. */
    if ((flags & UF2_FLAG_FAMILYID_PRESENT) == 0u) return UF2_ERR_NO_FAMILY;
    if (family_id != UF2_FAMILY_RP2040)            return UF2_ERR_WRONG_FAMILY;

    /* The block number must be less than the total block count. */
    if (block_no >= num_blocks) return UF2_ERR_BAD_BLOCK_NUMBER;

    out->target_addr = target_addr;
    out->length      = payload_size;
    out->data        = block + UF2_HEADER_SIZE;

    return UF2_OK;
}

/* -------------------------------------------------------------------------
 * Helper: read an entire file into memory.
 * Returns a malloc'd buffer; caller frees. On error, returns NULL.
 * ----------------------------------------------------------------------- */

static uint8_t *read_entire_file(const char *path, size_t *out_size) {
    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        fprintf(stderr, "error: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }

    if (fseek(fh, 0, SEEK_END) != 0) {
        fclose(fh);
        return NULL;
    }
    long file_size = ftell(fh);
    if (file_size < 0) {
        fclose(fh);
        return NULL;
    }
    rewind(fh);

    uint8_t *buf = (uint8_t *) malloc((size_t) file_size);
    if (buf == NULL) {
        fclose(fh);
        return NULL;
    }

    size_t nread = fread(buf, 1u, (size_t) file_size, fh);
    fclose(fh);
    if (nread != (size_t) file_size) {
        free(buf);
        return NULL;
    }

    *out_size = (size_t) file_size;
    return buf;
}

/* -------------------------------------------------------------------------
 * Pretty-print one block's header in a single line.
 * ----------------------------------------------------------------------- */

static void print_block_summary(uint32_t block_index,
                                const uint8_t *block,
                                const uf2_block_payload_t *payload) {
    uint32_t block_no   = cc_read_le32(block + 20);
    uint32_t num_blocks = cc_read_le32(block + 24);
    uint32_t family_id  = cc_read_le32(block + 28);

    printf("  block %3" PRIu32 "/%-3" PRIu32 "  "
           "target=0x%08" PRIx32 "  "
           "len=%4" PRIu32 "  "
           "family=0x%08" PRIx32 "\n",
           block_no, num_blocks - 1u,
           payload->target_addr,
           payload->length,
           family_id);

    (void) block_index;
}

/* -------------------------------------------------------------------------
 * Main: read the UF2 file, parse each block, print a summary.
 * ----------------------------------------------------------------------- */

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <file.uf2>\n", argv[0]);
        return EXIT_FAILURE;
    }

    size_t file_size = 0u;
    uint8_t *file = read_entire_file(argv[1], &file_size);
    if (file == NULL) {
        return EXIT_FAILURE;
    }

    if (file_size == 0u || (file_size % UF2_BLOCK_SIZE) != 0u) {
        fprintf(stderr, "error: file size %zu is not a multiple of %u\n",
                file_size, (unsigned) UF2_BLOCK_SIZE);
        free(file);
        return EXIT_FAILURE;
    }

    size_t block_count = file_size / UF2_BLOCK_SIZE;
    printf("UF2 file: %s\n", argv[1]);
    printf("  size:   %zu bytes\n", file_size);
    printf("  blocks: %zu\n", block_count);
    printf("\n");

    /* Aggregate stats and per-block detail. */
    uint32_t expected_total = 0u;
    uint32_t lowest_addr    = 0xFFFFFFFFu;
    uint32_t highest_addr   = 0u;
    uint32_t total_payload  = 0u;
    int prev_block_no       = -1;

    for (size_t i = 0u; i < block_count; i++) {
        const uint8_t *block = file + (i * UF2_BLOCK_SIZE);
        uf2_block_payload_t payload;

        uf2_result_t r = cc_uf2_parse_block(block, &payload);
        if (r != UF2_OK) {
            fprintf(stderr, "error: block %zu rejected with code %d\n", i, (int) r);
            free(file);
            return EXIT_FAILURE;
        }

        uint32_t block_no = cc_read_le32(block + 20);
        uint32_t num_blocks_field = cc_read_le32(block + 24);

        /* First block sets the expected total. */
        if (i == 0u) {
            expected_total = num_blocks_field;
            if (expected_total != block_count) {
                fprintf(stderr, "warning: numBlocks=%" PRIu32
                        " but file has %zu blocks\n",
                        expected_total, block_count);
            }
        }

        /* Strict in-order sequencing check. */
        if ((int32_t) block_no != prev_block_no + 1) {
            fprintf(stderr, "error: block %zu has blockNo=%" PRIu32
                    " but expected %d\n", i, block_no, prev_block_no + 1);
            free(file);
            return EXIT_FAILURE;
        }
        prev_block_no = (int32_t) block_no;

        if (payload.target_addr < lowest_addr)  lowest_addr  = payload.target_addr;
        if (payload.target_addr > highest_addr) highest_addr = payload.target_addr;
        total_payload += payload.length;

        print_block_summary((uint32_t) i, block, &payload);
    }

    printf("\n");
    printf("summary:\n");
    printf("  lowest target addr:  0x%08" PRIx32 "\n", lowest_addr);
    printf("  highest target addr: 0x%08" PRIx32 "\n", highest_addr);
    printf("  total payload bytes: %" PRIu32 "\n", total_payload);
    printf("  overhead:            %.1f%%\n",
           100.0 * (double)(file_size - total_payload) / (double) file_size);

    free(file);
    return EXIT_SUCCESS;
}

/* End of exercise-01-uf2-parser.c. */
