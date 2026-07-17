/*
 * rle2img — Convert RGB565 RLE data back to an image file
 *
 * Input:  .h  (C header with RLE array + dimension defines)
 *         .bin (raw RLE stream: 4-byte pixel_count header + body)
 * Output: PNG / JPEG / BMP / TGA (any format stb_image_write supports)
 *
 * Usage:
 *   rle2img [options] <input_file>
 *
 * Options:
 *   -o <file>    Output file path (default: <input>.png)
 *   -t <fmt>     Output format: png, jpg, bmp, tga (default: png)
 *   -w <width>   Image width (required for .bin input; auto-detected for .h)
 *   -h <height>  Image height (required for .bin input; auto-detected for .h)
 *   -q <n>       JPEG quality 1–100 (default: 95)
 *   --help       Show this help
 *
 * Dependencies: stb_image_write.h (auto-downloaded by CMake)
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "rgb565_rle.h"

/* ==========================================================================
 * RGB565 → RGB888 conversion
 * ========================================================================== */

static void rgb565_to_rgb888(uint16_t px, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint16_t r5 = (px >> 11) & 0x1Fu;
    uint16_t g6 = (px >>  5) & 0x3Fu;
    uint16_t b5 =  px        & 0x1Fu;

    /* scale up: 5-bit → 8-bit, 6-bit → 8-bit */
    *r = (uint8_t)((r5 * 255u + 15u) / 31u);
    *g = (uint8_t)((g6 * 255u + 31u) / 63u);
    *b = (uint8_t)((b5 * 255u + 15u) / 31u);
}

/*
 * Convert an array of RGB565 pixels to interleaved RGB888.
 * Caller frees `out`.
 */
static uint8_t *pixels_to_rgb888(const uint16_t *pixels,
                                 size_t count)
{
    uint8_t *dst;
    size_t   i;

    dst = (uint8_t *)malloc(count * 3u);
    if (dst == NULL) return NULL;

    for (i = 0u; i < count; i++) {
        rgb565_to_rgb888(pixels[i],
                         &dst[i * 3u + 0u],
                         &dst[i * 3u + 1u],
                         &dst[i * 3u + 2u]);
    }

    return dst;
}

/* ==========================================================================
 * .h file parser — extract dimensions and RLE bytes from a C header
 * ========================================================================== */

/*
 * Find a #define value by suffix.  Looks for:
 *   #define <prefix>_WIDTH  <value>
 *   #define <prefix>_HEIGHT <value>
 * Returns 0 if not found.
 */
static int parse_define_int(const char *text, const char *suffix, int *out)
{
    const char *p;
    const char *val_start;
    char        val_buf[32];
    size_t      vlen;

    p = text;
    while (*p != '\0') {
        /* look for "#define " */
        if (strncmp(p, "#define ", 8u) == 0) {
            const char *name_start = p + 8u;
            const char *name_end;

            /* skip whitespace after #define */
            while (*name_start == ' ' || *name_start == '\t')
                name_start++;

            name_end = name_start;
            while (*name_end != '\0' && *name_end != ' ' &&
                   *name_end != '\t' && *name_end != '\r' &&
                   *name_end != '\n')
                name_end++;

            /* check if name ends with suffix */
            {
                size_t name_len = (size_t)(name_end - name_start);
                size_t suf_len  = strlen(suffix);

                if (name_len > suf_len &&
                    strncmp(name_start + name_len - suf_len,
                            suffix, suf_len) == 0) {
                    /* find the value after the name */
                    val_start = name_end;
                    while (*val_start == ' ' || *val_start == '\t')
                        val_start++;

                    vlen = 0u;
                    while (val_start[vlen] >= '0' &&
                           val_start[vlen] <= '9' &&
                           vlen < sizeof(val_buf) - 1u) {
                        val_buf[vlen] = val_start[vlen];
                        vlen++;
                    }
                    val_buf[vlen] = '\0';

                    if (vlen > 0u) {
                        *out = atoi(val_buf);
                        return 1;
                    }
                }
            }
        }
        /* advance to next line */
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    return 0;
}

/*
 * Extract raw bytes from a C array body between { and }.
 * Parses hex literals like 0xAB.  Returns number of bytes read.
 */
static size_t parse_hex_array(const char *body, uint8_t *out, size_t max_out)
{
    const char *p = body;
    size_t      count = 0u;

    while (*p != '\0' && count < max_out) {
        /* scan for "0x" or "0X" */
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
            p[2] != '\0' && p[3] != '\0') {
            unsigned int val;
            char hex[3];

            hex[0] = p[2];
            hex[1] = p[3];
            hex[2] = '\0';

            if (sscanf(hex, "%2x", &val) == 1) {
                out[count++] = (uint8_t)val;
                p += 4;
                continue;
            }
        }
        p++;
    }

    return count;
}

/*
 * Load RLE data and dimensions from a .h file.
 * Returns 0 on success.  Caller frees *rle_out.
 */
static int load_rle_from_h(const char   *path,
                           int          *width_out,
                           int          *height_out,
                           uint8_t     **rle_out,
                           size_t       *rle_size_out)
{
    FILE   *fp;
    long    file_sz;
    char   *text;
    int     w = 0, h = 0;
    size_t  rle_sz;
    uint8_t *rle;
    int     ret = -1;

    *width_out  = 0;
    *height_out = 0;
    *rle_out    = NULL;
    *rle_size_out = 0u;

    /* read entire file */
    fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open '%s'.\n", path);
        return -1;
    }

    fseek(fp, 0L, SEEK_END);
    file_sz = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    if (file_sz <= 0L) {
        fprintf(stderr, "Error: file is empty.\n");
        fclose(fp);
        return -1;
    }

    text = (char *)malloc((size_t)(file_sz + 1L));
    if (text == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        fclose(fp);
        return -1;
    }

    if (fread(text, 1u, (size_t)file_sz, fp) != (size_t)file_sz) {
        fprintf(stderr, "Error: cannot read '%s'.\n", path);
        free(text);
        fclose(fp);
        return -1;
    }
    text[file_sz] = '\0';
    fclose(fp);

    /* extract dimensions from #define */
    if (parse_define_int(text, "_WIDTH",  &w) == 0 ||
        parse_define_int(text, "_HEIGHT", &h) == 0) {
        fprintf(stderr, "Error: cannot find _WIDTH/_HEIGHT defines in '%s'.\n",
                path);
        free(text);
        return -1;
    }

    if (w <= 0 || h <= 0) {
        fprintf(stderr, "Error: invalid dimensions %d×%d.\n", w, h);
        free(text);
        return -1;
    }

    /* allocate RLE buffer (worst-case size for dimensions) */
    {
        size_t max_rle = rgb565_rle_max_compressed_size((size_t)(w * h));
        rle = (uint8_t *)malloc(max_rle);
        if (rle == NULL) {
            fprintf(stderr, "Error: memory allocation failed.\n");
            free(text);
            return -1;
        }

        /* find the array body between { and } */
        {
            const char *brace = strchr(text, '{');
            if (brace == NULL) {
                fprintf(stderr, "Error: no array body found in '%s'.\n", path);
                free(rle);
                free(text);
                return -1;
            }

            rle_sz = parse_hex_array(brace + 1u, rle, max_rle);
            if (rle_sz < 4u) {
                fprintf(stderr, "Error: too few bytes in RLE array (%zu).\n",
                        rle_sz);
                free(rle);
                free(text);
                return -1;
            }
        }
    }

    *width_out    = w;
    *height_out   = h;
    *rle_out      = rle;
    *rle_size_out = rle_sz;
    ret           = 0;

    free(text);
    return ret;
}

/*
 * Load RLE data from a .bin file.  Dimensions must be provided.
 * Returns 0 on success.  Caller frees *rle_out.
 */
static int load_rle_from_bin(const char *path,
                             uint8_t **rle_out,
                             size_t   *rle_size_out)
{
    FILE   *fp;
    long    file_sz;
    uint8_t *rle;

    *rle_out      = NULL;
    *rle_size_out = 0u;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open '%s'.\n", path);
        return -1;
    }

    fseek(fp, 0L, SEEK_END);
    file_sz = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    if (file_sz <= 0L) {
        fprintf(stderr, "Error: file is empty.\n");
        fclose(fp);
        return -1;
    }

    rle = (uint8_t *)malloc((size_t)file_sz);
    if (rle == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        fclose(fp);
        return -1;
    }

    if (fread(rle, 1u, (size_t)file_sz, fp) != (size_t)file_sz) {
        fprintf(stderr, "Error: cannot read '%s'.\n", path);
        free(rle);
        fclose(fp);
        return -1;
    }

    fclose(fp);

    *rle_out      = rle;
    *rle_size_out = (size_t)file_sz;
    return 0;
}

/* ==========================================================================
 * Derive output filename
 * ========================================================================== */

static void make_output_name(const char *input,
                             const char *ext,
                             char *out, size_t out_sz)
{
    const char *dot;
    size_t      base_len;

    /* strip directory */
    {
        const char *slash = strrchr(input, '/');
        if (slash != NULL) input = slash + 1;
        else {
            slash = strrchr(input, '\\');
            if (slash != NULL) input = slash + 1;
        }
    }

    dot = strrchr(input, '.');
    if (dot != NULL) {
        base_len = (size_t)(dot - input);
    } else {
        base_len = strlen(input);
    }

    if (base_len > out_sz - strlen(ext) - 1u) {
        base_len = out_sz - strlen(ext) - 1u;
    }

    memcpy(out, input, base_len);
    out[base_len] = '\0';
    strcat(out, ext);
}

/* ==========================================================================
 * Usage
 * ========================================================================== */

static void print_usage(const char *prog)
{
    printf("Usage: %s [options] <input_file>\n\n", prog);
    printf("Convert RLE-compressed RGB565 data back to an image.\n\n");
    printf("Input formats:\n");
    printf("  .h   — C header (reads _WIDTH/_HEIGHT defines + hex array)\n");
    printf("  .bin — raw RLE stream  (needs -w and -h)\n\n");
    printf("Options:\n");
    printf("  -o <file>    Output file (default: <input>.png)\n");
    printf("  -t <fmt>     Output format: png, jpg, bmp, tga (default: png)\n");
    printf("  -w <width>   Image width (required for .bin; auto for .h)\n");
    printf("  -h <height>  Image height (required for .bin; auto for .h)\n");
    printf("  -q <n>       JPEG quality 1–100 (default: 95)\n");
    printf("  --help       Show this help\n");
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(int argc, char **argv)
{
    const char *input_path  = NULL;
    const char *output_path = NULL;
    const char *format      = "png";
    int         req_w       = 0;
    int         req_h       = 0;
    int         jpg_quality = 95;
    int         i;

    int         in_w, in_h;
    uint8_t    *rle_data     = NULL;
    size_t      rle_size;
    int         from_hdr      = 0; /* 1 = from .h, 0 = from .bin */
    uint16_t   *pixels       = NULL;
    size_t      pixel_count;
    uint8_t    *rgb888        = NULL;
    int         ret           = EXIT_FAILURE;

    char        path_buf[512];

    /* --- parse arguments --- */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "-o") == 0 && (i + 1 < argc)) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 && (i + 1 < argc)) {
            format = argv[++i];
        } else if (strcmp(argv[i], "-w") == 0 && (i + 1 < argc)) {
            req_w = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 && (i + 1 < argc)) {
            req_h = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-q") == 0 && (i + 1 < argc)) {
            jpg_quality = atoi(argv[++i]);
            if (jpg_quality < 1)  jpg_quality = 1;
            if (jpg_quality > 100) jpg_quality = 100;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: unknown option '%s'.\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        } else {
            input_path = argv[i];
        }
    }

    if (input_path == NULL) {
        fprintf(stderr, "Error: no input file specified.\n\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* --- detect input type by extension --- */
    {
        const char *ext = strrchr(input_path, '.');
        if (ext != NULL && strcmp(ext, ".h") == 0) {
            if (load_rle_from_h(input_path, &in_w, &in_h,
                                &rle_data, &rle_size) != 0) {
                goto done;
            }
            from_hdr = 1;

            /* command-line -w/-h override header defines */
            if (req_w > 0) in_w = req_w;
            if (req_h > 0) in_h = req_h;

            printf("  Input      : %s (C header)\n", input_path);
            printf("  Dimensions : %d × %d\n", in_w, in_h);
        } else {
            in_w = req_w;
            in_h = req_h;

            if (in_w <= 0 || in_h <= 0) {
                fprintf(stderr,
                        "Error: -w and -h are required for .bin input.\n");
                goto done;
            }

            if (load_rle_from_bin(input_path, &rle_data, &rle_size) != 0) {
                goto done;
            }

            printf("  Input      : %s (binary)\n", input_path);
            printf("  Dimensions : %d × %d (user-specified)\n", in_w, in_h);
        }
    }

    /* --- validate RLE header --- */
    if (rle_size < 4u) {
        fprintf(stderr, "Error: RLE data too small (%zu bytes).\n", rle_size);
        goto done;
    }

    {
        uint32_t declared = (uint32_t)rle_data[0]
                          | ((uint32_t)rle_data[1] << 8)
                          | ((uint32_t)rle_data[2] << 16)
                          | ((uint32_t)rle_data[3] << 24);

        pixel_count = (size_t)(in_w * in_h);

        if (declared != (uint32_t)pixel_count) {
            fprintf(stderr,
                    "Warning: RLE header declares %u pixels, "
                    "but %d×%d = %zu.\n",
                    declared, in_w, in_h, pixel_count);
            fprintf(stderr,
                    "  Using header pixel count: %u.\n", declared);
            pixel_count = (size_t)declared;
        }

        printf("  Pixels     : %zu (%.1f KB raw)\n",
               pixel_count, (double)(pixel_count * 2u) / 1024.0);
        printf("  RLE size   : %zu bytes (%.1f%% of raw)\n",
               rle_size,
               (double)rle_size / (double)(pixel_count * 2u) * 100.0);
    }

    /* --- decompress --- */
    pixels = (uint16_t *)malloc(pixel_count * sizeof(uint16_t));
    if (pixels == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        goto done;
    }

    {
        size_t dp = rgb565_rle_decompress(rle_data, rle_size,
                                           pixels, pixel_count);
        if (dp == 0u) {
            fprintf(stderr, "Error: decompression failed "
                    "(corrupted data?).\n");
            goto done;
        }
        if (dp != pixel_count) {
            fprintf(stderr,
                    "Warning: decompressed %zu pixels, expected %zu.\n",
                    dp, pixel_count);
            pixel_count = dp;
        }
    }

    /* --- convert to RGB888 --- */
    rgb888 = pixels_to_rgb888(pixels, pixel_count);
    if (rgb888 == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        goto done;
    }

    /* --- determine output path --- */
    if (output_path == NULL) {
        char ext_buf[16];
        snprintf(ext_buf, sizeof(ext_buf), ".%s", format);
        make_output_name(input_path, ext_buf, path_buf, sizeof(path_buf));
        output_path = path_buf;
    }

    /* --- write image --- */
    {
        int write_ok = 0;
        int w = in_w;
        int h = (int)(pixel_count / (size_t)in_w);

        if ((size_t)(w * h) != pixel_count) {
            fprintf(stderr,
                    "Warning: %d×%d ≠ %zu pixels. "
                    "Adjusting height to fit.\n",
                    w, h, pixel_count);
            /* try to find a height that fits */
            if (w > 0) {
                h = (int)(pixel_count / (size_t)w);
            } else {
                fprintf(stderr, "Error: invalid width.\n");
                goto done;
            }
        }

        printf("  Output     : %s (%s, %d×%d)\n",
               output_path, format, w, h);

        if (strcmp(format, "png") == 0) {
            write_ok = stbi_write_png(output_path, w, h, 3,
                                      rgb888, w * 3);
        } else if (strcmp(format, "jpg") == 0 ||
                   strcmp(format, "jpeg") == 0) {
            write_ok = stbi_write_jpg(output_path, w, h, 3,
                                      rgb888, jpg_quality);
        } else if (strcmp(format, "bmp") == 0) {
            write_ok = stbi_write_bmp(output_path, w, h, 3,
                                      rgb888);
        } else if (strcmp(format, "tga") == 0) {
            write_ok = stbi_write_tga(output_path, w, h, 3,
                                      rgb888);
        } else {
            fprintf(stderr, "Error: unsupported format '%s'.\n", format);
            fprintf(stderr, "  Supported: png, jpg, bmp, tga\n");
            goto done;
        }

        if (!write_ok) {
            fprintf(stderr, "Error: failed to write '%s'.\n", output_path);
            goto done;
        }
    }

    printf("\nDone.\n\n");
    ret = EXIT_SUCCESS;

done:
    free(rgb888);
    free(pixels);
    free(rle_data);

    return ret;
}
