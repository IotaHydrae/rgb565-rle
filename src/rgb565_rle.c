/*
 * RGB565 RLE Compression Library — Implementation
 *
 * Platform-independent.  Uses only <stddef.h> and <stdint.h>.
 * No dynamic allocation, no libc calls, no platform-specific APIs.
 *
 * SPDX-License-Identifier: MIT
 */

#include "rgb565_rle.h"

/* --------------------------------------------------------------------------
 * Internal constants
 * -------------------------------------------------------------------------- */

/** Bit mask for the run-type flag in a control byte. */
#define CTL_REPEAT  0x80u

/** Bit mask for the run-length field (bits 6..0). */
#define CTL_LENGTH  0x7Fu

/** Header size in bytes (uint32_t pixel_count, little-endian). */
#define HEADER_SIZE 4u

/* --------------------------------------------------------------------------
 * Helper: write a 16-bit pixel as two little-endian bytes
 * -------------------------------------------------------------------------- */

static void write_pixel_le(uint8_t *dst, uint16_t pixel)
{
    dst[0] = (uint8_t)(pixel & 0xFFu);
    dst[1] = (uint8_t)((pixel >> 8) & 0xFFu);
}

/* --------------------------------------------------------------------------
 * Helper: read a 16-bit little-endian pixel from two bytes
 * -------------------------------------------------------------------------- */

static uint16_t read_pixel_le(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

/* --------------------------------------------------------------------------
 * Helper: write a 32-bit little-endian value
 * -------------------------------------------------------------------------- */

static void write_u32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

/* --------------------------------------------------------------------------
 * Helper: read a 32-bit little-endian value
 * -------------------------------------------------------------------------- */

static uint32_t read_u32_le(const uint8_t *src)
{
    return (uint32_t)src[0]
         | ((uint32_t)src[1] << 8)
         | ((uint32_t)src[2] << 16)
         | ((uint32_t)src[3] << 24);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

size_t rgb565_rle_max_compressed_size(size_t pixel_count)
{
    if (pixel_count == 0u) {
        return 0u;
    }

    /*
     * Worst case: every pixel is a 1-pixel literal run.
     * Total = HEADER_SIZE + pixel_count * 3.
     *
     * Check for overflow before multiplying.
     */
    if (pixel_count > (SIZE_MAX - HEADER_SIZE) / 3u) {
        return 0u;  /* would overflow size_t */
    }

    return HEADER_SIZE + pixel_count * 3u;
}

size_t rgb565_rle_compress(const uint16_t *pixels,
                           size_t pixel_count,
                           uint8_t *output,
                           size_t output_capacity)
{
    size_t pos;      /* current read position in pixels[] */
    uint8_t *dst;    /* current write pointer in output[] */

    /* --- parameter validation --- */
    if ((pixels == NULL) || (output == NULL) || (pixel_count == 0u)) {
        return 0u;
    }

    if (output_capacity < rgb565_rle_max_compressed_size(pixel_count)) {
        return 0u;
    }

    /* --- write header --- */
    write_u32_le(output, (uint32_t)pixel_count);
    dst  = output + HEADER_SIZE;
    pos  = 0u;

    /* --- compress --- */
    while (pos < pixel_count) {
        size_t run_len;
        size_t lit_len;

        /*
         * Look ahead for a repeat run (two or more identical consecutive
         * pixels).  We only start a repeat run when the repetition is at
         * least 2 pixels, because a run of 1 as a repeat would cost exactly
         * the same as a literal (1 ctl + 2 data = 3 bytes) but a repeat
         * prevents merging with following non-repeating pixels in the same
         * literal run.
         */
        run_len = 1u;
        while ((pos + run_len < pixel_count) &&
               (run_len < RGB565_RLE_MAX_RUN_LENGTH) &&
               (pixels[pos + run_len] == pixels[pos])) {
            run_len++;
        }

        if (run_len >= 2u) {
            /* --- repeat run --- */
            *dst++ = (uint8_t)(CTL_REPEAT | (run_len - 1u));
            write_pixel_le(dst, pixels[pos]);
            dst += 2;
            pos += run_len;
        } else {
            /*
             * --- literal run ---
             * Gather as many consecutive non-repeating pixels as possible.
             * We stop when we encounter a repeat of at least 2.
             */
            lit_len = 1u;
            while ((pos + lit_len < pixel_count) &&
                   (lit_len < RGB565_RLE_MAX_RUN_LENGTH)) {
                /*
                 * Check if the pixel at (pos + lit_len) equals the previous
                 * pixel — if so, a repeat run would start here.  However we
                 * only break if there are at least 2 identical pixels ahead
                 * (otherwise it isn't a beneficial repeat run).
                 */
                if ((pos + lit_len + 1u < pixel_count) &&
                    (pixels[pos + lit_len] == pixels[pos + lit_len + 1u])) {
                    break;
                }
                lit_len++;
            }

            *dst++ = (uint8_t)(lit_len - 1u);  /* bit 7 = 0 → literal */

            {
                size_t j;
                for (j = 0u; j < lit_len; j++) {
                    write_pixel_le(dst, pixels[pos + j]);
                    dst += 2;
                }
            }

            pos += lit_len;
        }
    }

    return (size_t)(dst - output);
}

size_t rgb565_rle_decompress(const uint8_t *input,
                             size_t input_size,
                             uint16_t *pixels,
                             size_t pixel_capacity)
{
    uint32_t total_pixels;
    size_t   pixel_count;   /* number of pixels emitted so far */
    size_t   pos;           /* read position in input[] */

    /* --- parameter validation --- */
    if ((input == NULL) || (pixels == NULL) || (input_size < HEADER_SIZE)) {
        return 0u;
    }

    /* --- read header --- */
    total_pixels = read_u32_le(input);

    if (total_pixels == 0u) {
        return 0u;  /* empty image is not meaningful */
    }

    if (total_pixels > pixel_capacity) {
        return 0u;  /* output buffer too small */
    }

    pixel_count = 0u;
    pos         = HEADER_SIZE;

    /* --- decompress --- */
    while (pos < input_size) {
        uint8_t  ctl;
        uint8_t  run_type;
        size_t   run_len;
        uint16_t pixel_val;

        if (pixel_count >= total_pixels) {
            /*
             * We already emitted all expected pixels; any trailing data
             * is ignored (future extensions could add metadata here).
             */
            break;
        }

        ctl      = input[pos++];
        run_type = ctl & CTL_REPEAT;
        run_len  = ((size_t)(ctl & CTL_LENGTH)) + 1u;

        if (run_type == CTL_REPEAT) {
            /* --- repeat run --- */
            if (pos + 2u > input_size) {
                return 0u;  /* truncated input */
            }

            pixel_val = read_pixel_le(&input[pos]);
            pos += 2;

            if (pixel_count + run_len > total_pixels) {
                return 0u;  /* would exceed declared pixel count */
            }

            {
                size_t i;
                for (i = 0u; i < run_len; i++) {
                    pixels[pixel_count + i] = pixel_val;
                }
            }
            pixel_count += run_len;
        } else {
            /* --- literal run --- */
            if (pos + run_len * 2u > input_size) {
                return 0u;  /* truncated input */
            }

            if (pixel_count + run_len > total_pixels) {
                return 0u;  /* would exceed declared pixel count */
            }

            {
                size_t i;
                for (i = 0u; i < run_len; i++) {
                    pixels[pixel_count + i] = read_pixel_le(&input[pos]);
                    pos += 2;
                }
            }
            pixel_count += run_len;
        }
    }

    /* --- verify we emitted exactly the declared number of pixels --- */
    if (pixel_count != total_pixels) {
        return 0u;  /* truncated stream */
    }

    return pixel_count;
}

size_t rgb565_rle_decompress_callback(const uint8_t *input,
                                      size_t input_size,
                                      uint16_t width,
                                      uint16_t *buf_a,
                                      uint16_t *buf_b,
                                      size_t buf_capacity,
                                      rgb565_rle_callback callback,
                                      void *user_data)
{
    uint32_t  total_pixels;
    size_t    pixel_count;   /* number of pixels emitted so far */
    size_t    pos;           /* read position in input[] */
    uint16_t  x;             /* current column within the image */
    uint16_t  y;             /* current row within the image */
    size_t    acc_count;     /* pixels accumulated in active buf */
    uint16_t  acc_x;         /* starting column of current batch */
    uint16_t  acc_y;         /* row of current batch */
    uint16_t *buf;           /* active buffer (buf_a, or toggles) */

    /* --- parameter validation --- */
    if ((input == NULL) || (callback == NULL) || (buf_a == NULL) ||
        (input_size < HEADER_SIZE) || (width == 0u) || (buf_capacity == 0u)) {
        return 0u;
    }

    /* --- read header --- */
    total_pixels = read_u32_le(input);

    if (total_pixels == 0u) {
        return 0u;  /* empty image is not meaningful */
    }

    pixel_count = 0u;
    pos         = HEADER_SIZE;
    x           = 0u;
    y           = 0u;
    acc_count   = 0u;
    acc_x       = 0u;
    acc_y       = 0u;
    buf         = buf_a;

    /*
     * Helper macro: flush accumulated pixels to the callback.
     * Computes the bounding rectangle which may span multiple rows.
     * When buf_b is non-NULL the active buffer toggles (ping-pong).
     */
#define FLUSH()                                                      \
    do {                                                             \
        uint32_t lp;                                                 \
        lp = (uint32_t)acc_y * width + acc_x + acc_count - 1u;      \
        callback(buf, acc_count, acc_x, acc_y,                       \
                 (uint16_t)(lp % width), (uint16_t)(lp / width),     \
                 user_data);                                         \
        acc_count = 0u;                                              \
        acc_x     = x;                                               \
        acc_y     = y;                                               \
        if (buf_b != NULL) {                                         \
            buf = (buf == buf_a) ? buf_b : buf_a;                    \
        }                                                            \
    } while (0)

    /* --- decompress --- */
    while (pos < input_size) {
        uint8_t  ctl;
        uint8_t  run_type;
        size_t   run_len;
        size_t   remaining;

        if (pixel_count >= total_pixels) {
            break;
        }

        ctl      = input[pos++];
        run_type = ctl & CTL_REPEAT;
        run_len  = ((size_t)(ctl & CTL_LENGTH)) + 1u;

        if (run_type == CTL_REPEAT) {
            /* --- repeat run — expand directly into caller's buf --- */
            uint16_t pixel_val;
            size_t   i;

            if (pos + 2u > input_size) {
                return 0u;  /* truncated input */
            }

            if (pixel_count + run_len > total_pixels) {
                return 0u;  /* would exceed declared pixel count */
            }

            pixel_val = read_pixel_le(&input[pos]);
            pos += 2;

            /*
             * Fast path: entire run fits in the current row AND in
             * the remaining buffer space.  Skip the chunk loop.
             */
            if ((x + run_len <= width) &&
                (acc_count + run_len <= buf_capacity)) {

                if (acc_count == 0u) {
                    acc_x = x;
                    acc_y = y;
                }

                for (i = 0u; i < run_len; i++) {
                    buf[acc_count + i] = pixel_val;
                }

                acc_count += run_len;
                x = (uint16_t)(x + run_len);

                if (x >= width) {
                    x = 0u;
                    y++;
                }

                if (acc_count >= buf_capacity) {
                    FLUSH();
                }
            } else {
                /* slow path — chunk at row / buffer boundaries */
                remaining = run_len;
                while (remaining > 0u) {
                    uint16_t space_in_row;
                    size_t   space_in_buf;
                    size_t   chunk;

                    space_in_row = (uint16_t)(width - x);
                    space_in_buf = buf_capacity - acc_count;

                    chunk = remaining;
                    if ((size_t)space_in_row < chunk) {
                        chunk = (size_t)space_in_row;
                    }
                    if (space_in_buf < chunk) {
                        chunk = space_in_buf;
                    }

                    if (acc_count == 0u) {
                        acc_x = x;
                        acc_y = y;
                    }

                    for (i = 0u; i < chunk; i++) {
                        buf[acc_count + i] = pixel_val;
                    }

                    acc_count  += chunk;
                    remaining  -= chunk;
                    x = (uint16_t)(x + chunk);

                    if (x >= width) {
                        x = 0u;
                        y++;
                    }

                    if (acc_count >= buf_capacity) {
                        FLUSH();
                    }
                }
            }
        } else {
            /* --- literal run — read directly into caller's buf --- */
            size_t i;

            if (pos + run_len * 2u > input_size) {
                return 0u;  /* truncated input */
            }

            if (pixel_count + run_len > total_pixels) {
                return 0u;  /* would exceed declared pixel count */
            }

            /*
             * Fast path: entire run fits in the current row AND in
             * the remaining buffer space.  Skip the chunk loop.
             */
            if ((x + run_len <= width) &&
                (acc_count + run_len <= buf_capacity)) {

                if (acc_count == 0u) {
                    acc_x = x;
                    acc_y = y;
                }

                for (i = 0u; i < run_len; i++) {
                    buf[acc_count + i] = read_pixel_le(&input[pos]);
                    pos += 2;
                }

                acc_count += run_len;
                x = (uint16_t)(x + run_len);

                if (x >= width) {
                    x = 0u;
                    y++;
                }

                if (acc_count >= buf_capacity) {
                    FLUSH();
                }
            } else {
                /* slow path — chunk at row / buffer boundaries */
                remaining = run_len;
                while (remaining > 0u) {
                    uint16_t space_in_row;
                    size_t   space_in_buf;
                    size_t   chunk;

                    space_in_row = (uint16_t)(width - x);
                    space_in_buf = buf_capacity - acc_count;

                    chunk = remaining;
                    if ((size_t)space_in_row < chunk) {
                        chunk = (size_t)space_in_row;
                    }
                    if (space_in_buf < chunk) {
                        chunk = space_in_buf;
                    }

                    if (acc_count == 0u) {
                        acc_x = x;
                        acc_y = y;
                    }

                    for (i = 0u; i < chunk; i++) {
                        buf[acc_count + i] = read_pixel_le(&input[pos]);
                        pos += 2;
                    }

                    acc_count  += chunk;
                    remaining  -= chunk;
                    x = (uint16_t)(x + chunk);

                    if (x >= width) {
                        x = 0u;
                        y++;
                    }

                    if (acc_count >= buf_capacity) {
                        FLUSH();
                    }
                }
            }
        }

        pixel_count += run_len;
    }

    /* --- final flush --- */
    if (acc_count > 0u) {
        uint32_t last_pos;

        last_pos = (uint32_t)acc_y * width
                 + acc_x + acc_count - 1u;
        callback(buf, acc_count, acc_x, acc_y,
                 (uint16_t)(last_pos % width),
                 (uint16_t)(last_pos / width),
                 user_data);
    }

#undef FLUSH

    /* --- verify we emitted exactly the declared number of pixels --- */
    if (pixel_count != total_pixels) {
        return 0u;  /* truncated stream */
    }

    return pixel_count;
}
