/*
 * rle_utils.h — Shared utilities for the conversion tools
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RLE_UTILS_H
#define RLE_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* --------------------------------------------------------------------------
 * Write compressed data as a C header file
 *
 * Produces a .h file with:
 *   - dimension #defines
 *   - compressed size #define
 *   - static const uint8_t array of the RLE data
 *
 * Returns 0 on success, non-zero on failure.
 * -------------------------------------------------------------------------- */

int write_rle_header(FILE       *out,
                     const char  *array_name,
                     const char  *source_name,
                     int          width,
                     int          height,
                     const uint8_t *rle_data,
                     size_t       rle_size,
                     size_t       raw_size);

/* --------------------------------------------------------------------------
 * Write compressed data as a raw binary file (.bin)
 *
 * The file contains only the RLE stream, identical to what
 * rgb565_rle_compress() produces.  Dimensions must be recovered from
 * the RLE header at decode time.
 *
 * Returns 0 on success, non-zero on failure.
 * -------------------------------------------------------------------------- */

int write_rle_binary(FILE *out, const uint8_t *rle_data, size_t rle_size);

/* --------------------------------------------------------------------------
 * Print a size-comparison summary line
 * -------------------------------------------------------------------------- */

void print_size_comparison(const char *label,
                           size_t raw_bytes,
                           size_t compressed_bytes);

#endif /* RLE_UTILS_H */
