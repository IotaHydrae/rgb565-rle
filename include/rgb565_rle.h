/*
 * RGB565 RLE Compression Library
 *
 * A platform-independent C library for compressing and decompressing
 * RGB565 pixel data using Run-Length Encoding (RLE).
 *
 * This library is designed to work in freestanding environments,
 * including bare-metal MCUs, MPUs, Linux userspace, and kernel space.
 * It depends only on freestanding C99 headers: <stddef.h> and <stdint.h>.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RGB565_RLE_H
#define RGB565_RLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------
 * Compressed Data Format
 *---------------------------------------------------------------------------
 *
 * The compressed stream consists of a 4-byte header followed by a sequence
 * of control + data blocks:
 *
 *   Header: uint32_t pixel_count (little-endian)
 *   Body:   repeated [control_byte][pixel_data...]
 *
 * Control byte encoding:
 *   Bit 7:     0 = literal run, 1 = repeat run
 *   Bits 6..0: run_length - 1  (0 represents 1 pixel, 127 represents 128)
 *
 * A literal run contains 'length' distinct pixels, each stored as 2 bytes
 * (little-endian RGB565).
 *
 * A repeat run contains one pixel value (2 bytes, little-endian) that
 * repeats 'length' times.
 *
 * All multi-byte values are stored in little-endian byte order, making
 * the compressed format portable across platforms regardless of native
 * endianness.
 */

/*---------------------------------------------------------------------------
 * Run-length limits
 *--------------------------------------------------------------------------- */

/** Maximum number of pixels in a single RLE run. */
#define RGB565_RLE_MAX_RUN_LENGTH 128u

/*---------------------------------------------------------------------------
 * API
 *--------------------------------------------------------------------------- */

/**
 * Calculate the worst-case compressed buffer size.
 *
 * In the worst case (e.g., alternating pixel values A, B, A, B...),
 * each pixel requires its own literal run of length 1:
 *   1 control byte + 2 data bytes = 3 bytes per pixel.
 *
 * Total worst-case: 4 (header) + pixel_count * 3.
 *
 * @param pixel_count  Number of RGB565 pixels to compress.
 * @return  Maximum possible compressed size in bytes,
 *          or 0 if pixel_count is 0 or would cause integer overflow.
 */
size_t rgb565_rle_max_compressed_size(size_t pixel_count);

/**
 * Compress an array of RGB565 pixels.
 *
 * @param pixels           Input pixel array (uint16_t per pixel, RGB565).
 * @param pixel_count      Number of pixels in the input.
 * @param output           Output buffer for compressed data.
 * @param output_capacity  Size of the output buffer in bytes.
 * @return  Number of bytes written to output on success,
 *          or 0 on error (NULL pointer, zero count, insufficient capacity).
 */
size_t rgb565_rle_compress(const uint16_t *pixels,
                           size_t pixel_count,
                           uint8_t *output,
                           size_t output_capacity);

/**
 * Decompress an RLE-compressed stream back to RGB565 pixels.
 *
 * @param input           Input buffer containing compressed data.
 * @param input_size      Size of the input buffer in bytes.
 * @param pixels          Output buffer for decompressed pixels.
 * @param pixel_capacity  Maximum number of pixels the output can hold.
 * @return  Number of pixels written on success,
 *          or 0 on error (NULL pointer, malformed data, insufficient capacity,
 *          truncated input).
 */
size_t rgb565_rle_decompress(const uint8_t *input,
                             size_t input_size,
                             uint16_t *pixels,
                             size_t pixel_capacity);

#ifdef __cplusplus
}
#endif

#endif /* RGB565_RLE_H */
