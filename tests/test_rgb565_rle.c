/*
 * test_rgb565_rle — Unit tests for the RGB565 RLE library
 *
 * Uses a minimal inline test harness (no external framework).
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rgb565_rle.h"

/* ==========================================================================
 * Minimal test harness
 * ========================================================================== */

static int   g_tests_run  = 0;
static int   g_tests_pass = 0;
static int   g_tests_fail = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void test_##name(void)

#define RUN_TEST(name) do {                          \
    g_tests_run++;                                   \
    printf("  [RUN ] %s\n", #name);                  \
    test_##name();                                   \
    printf("  [PASS] %s\n", #name);                  \
    g_tests_pass++;                                  \
} while (0)

#define ASSERT_TRUE(cond) do {                       \
    if (!(cond)) {                                   \
        printf("  [FAIL] %s:%d: ASSERT_TRUE(%s)\n",  \
               __FILE__, __LINE__, #cond);           \
        g_tests_fail++;                              \
        return;                                      \
    }                                                \
} while (0)

#define ASSERT_EQ(a, b) do {                         \
    if ((a) != (b)) {                                \
        printf("  [FAIL] %s:%d: ASSERT_EQ(%s, %s) "  \
               "(%zu vs %zu)\n",                     \
               __FILE__, __LINE__, #a, #b,           \
               (size_t)(a), (size_t)(b));            \
        g_tests_fail++;                              \
        return;                                      \
    }                                                \
} while (0)

#define ASSERT_U16_EQ(a, b) do {                     \
    if ((a) != (b)) {                                \
        printf("  [FAIL] %s:%d: "                    \
               "ASSERT_U16_EQ(%s, %s) "              \
               "(0x%04X vs 0x%04X)\n",               \
               __FILE__, __LINE__, #a, #b,           \
               (unsigned int)(a),                    \
               (unsigned int)(b));                   \
        g_tests_fail++;                              \
        return;                                      \
    }                                                \
} while (0)

/* ==========================================================================
 * Helper: round-trip pixels through compress → decompress
 * ========================================================================== */

static int round_trip(const uint16_t *pixels, size_t pixel_count)
{
    size_t   max_size;
    uint8_t *compressed;
    size_t   comp_size;
    uint16_t *decompressed;
    size_t   decomp_count;
    size_t   i;
    int      ok = 1;

    max_size = rgb565_rle_max_compressed_size(pixel_count);
    if (max_size == 0u) {
        return 0;
    }

    compressed = (uint8_t *)malloc(max_size);
    if (compressed == NULL) {
        return 0;
    }

    comp_size = rgb565_rle_compress(pixels, pixel_count,
                                    compressed, max_size);
    if (comp_size == 0u || comp_size > max_size) {
        free(compressed);
        return 0;
    }

    decompressed = (uint16_t *)malloc(pixel_count * sizeof(uint16_t));
    if (decompressed == NULL) {
        free(compressed);
        return 0;
    }

    decomp_count = rgb565_rle_decompress(compressed, comp_size,
                                         decompressed, pixel_count);
    if (decomp_count != pixel_count) {
        free(decompressed);
        free(compressed);
        return 0;
    }

    for (i = 0u; i < pixel_count; i++) {
        if (decompressed[i] != pixels[i]) {
            ok = 0;
            break;
        }
    }

    free(decompressed);
    free(compressed);
    return ok;
}

/* ==========================================================================
 * Test cases
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * max_compressed_size
 * -------------------------------------------------------------------------- */

TEST(max_size_zero_pixels)
{
    ASSERT_EQ(rgb565_rle_max_compressed_size(0u), 0u);
}

TEST(max_size_single_pixel)
{
    /* 4 header + 1 * 3 = 7 */
    ASSERT_EQ(rgb565_rle_max_compressed_size(1u), 7u);
}

TEST(max_size_typical)
{
    /* 4 + 256 * 3 = 772 */
    ASSERT_EQ(rgb565_rle_max_compressed_size(256u), 772u);
}

TEST(max_size_overflow)
{
    /* SIZE_MAX / 3 is the threshold */
    ASSERT_EQ(rgb565_rle_max_compressed_size(SIZE_MAX), 0u);
}

/* --------------------------------------------------------------------------
 * Compress / decompress — parameter validation
 * -------------------------------------------------------------------------- */

TEST(compress_null_pixels)
{
    uint8_t buf[64];
    ASSERT_EQ(rgb565_rle_compress(NULL, 16u, buf, sizeof(buf)), 0u);
}

TEST(compress_null_output)
{
    uint16_t pixels[16];
    memset(pixels, 0, sizeof(pixels));
    ASSERT_EQ(rgb565_rle_compress(pixels, 16u, NULL, 64u), 0u);
}

TEST(compress_zero_count)
{
    uint8_t buf[64];
    ASSERT_EQ(rgb565_rle_compress(NULL, 0u, buf, sizeof(buf)), 0u);
}

TEST(compress_insufficient_capacity)
{
    uint16_t pixels[16];
    uint8_t  buf[3];  /* way too small */
    memset(pixels, 0, sizeof(pixels));
    ASSERT_EQ(rgb565_rle_compress(pixels, 16u, buf, sizeof(buf)), 0u);
}

TEST(decompress_null_input)
{
    uint16_t pixels[16];
    ASSERT_EQ(rgb565_rle_decompress(NULL, 64u, pixels, 16u), 0u);
}

TEST(decompress_null_output)
{
    uint8_t buf[64];
    ASSERT_EQ(rgb565_rle_decompress(buf, sizeof(buf), NULL, 16u), 0u);
}

TEST(decompress_too_small_input)
{
    uint8_t  buf[2];  /* smaller than header */
    uint16_t pixels[16];
    ASSERT_EQ(rgb565_rle_decompress(buf, sizeof(buf), pixels, 16u), 0u);
}

/* --------------------------------------------------------------------------
 * Round-trip: basic patterns
 * -------------------------------------------------------------------------- */

TEST(roundtrip_single_pixel)
{
    uint16_t pixels[] = { 0xF800u };  /* pure red */
    ASSERT_TRUE(round_trip(pixels, 1u));
}

TEST(roundtrip_two_identical)
{
    uint16_t pixels[] = { 0x07E0u, 0x07E0u };  /* both pure green */
    ASSERT_TRUE(round_trip(pixels, 2u));
}

TEST(roundtrip_two_different)
{
    uint16_t pixels[] = { 0xF800u, 0x001Fu };  /* red, blue */
    ASSERT_TRUE(round_trip(pixels, 2u));
}

TEST(roundtrip_solid_color)
{
    uint16_t pixels[256];
    size_t i;

    for (i = 0u; i < 256u; i++) {
        pixels[i] = 0xFFFFu;  /* white */
    }
    ASSERT_TRUE(round_trip(pixels, 256u));

    /* Solid colour should compress very well */
    {
        size_t   max_sz = rgb565_rle_max_compressed_size(256u);
        uint8_t *comp   = (uint8_t *)malloc(max_sz);
        size_t   sz     = rgb565_rle_compress(pixels, 256u, comp, max_sz);

        ASSERT_TRUE(sz > 0u);
        /*
         * 256 identical pixels: should fit in 2 repeat runs of 128.
         * Header (4) + 2 × (1 ctl + 2 data) = 4 + 6 = 10.
         */
        ASSERT_TRUE(sz <= 10u);
        free(comp);
    }
}

TEST(roundtrip_alternating)
{
    uint16_t pixels[128];
    size_t i;

    for (i = 0u; i < 128u; i++) {
        pixels[i] = (i & 1u) ? 0xF800u : 0x001Fu;  /* red, blue, red, blue... */
    }
    ASSERT_TRUE(round_trip(pixels, 128u));
}

TEST(roundtrip_gradient)
{
    uint16_t pixels[256];
    size_t i;

    for (i = 0u; i < 256u; i++) {
        uint16_t r = (uint16_t)((i * 31u) / 256u) & 0x1Fu;
        uint16_t g = (uint16_t)((i * 63u) / 256u) & 0x3Fu;
        uint16_t b = (uint16_t)((i * 31u) / 256u) & 0x1Fu;
        pixels[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
    ASSERT_TRUE(round_trip(pixels, 256u));
}

/* --------------------------------------------------------------------------
 * Round-trip: edge cases around max run length (128)
 * -------------------------------------------------------------------------- */

TEST(roundtrip_run_exactly_128)
{
    uint16_t pixels[128];
    size_t i;

    for (i = 0u; i < 128u; i++) {
        pixels[i] = 0xAAAAu;
    }
    ASSERT_TRUE(round_trip(pixels, 128u));
}

TEST(roundtrip_run_129_same)
{
    uint16_t pixels[129];
    size_t i;

    for (i = 0u; i < 129u; i++) {
        pixels[i] = 0x5555u;
    }
    ASSERT_TRUE(round_trip(pixels, 129u));
}

TEST(roundtrip_literal_exactly_128)
{
    /* 128 distinct values — forces max-size literal run */
    uint16_t pixels[128];
    size_t i;

    for (i = 0u; i < 128u; i++) {
        pixels[i] = (uint16_t)(i & 0xFFFFu);
    }
    ASSERT_TRUE(round_trip(pixels, 128u));
}

/* --------------------------------------------------------------------------
 * Decompress: corrupted / truncated data
 * -------------------------------------------------------------------------- */

TEST(decompress_truncated_body)
{
    uint16_t pixels_in[]  = { 0x1234u, 0x5678u, 0x9ABCu };
    size_t   max_sz       = rgb565_rle_max_compressed_size(3u);
    uint8_t *comp         = (uint8_t *)malloc(max_sz);
    size_t   comp_sz;
    uint16_t pixels_out[16];

    comp_sz = rgb565_rle_compress(pixels_in, 3u, comp, max_sz);
    ASSERT_TRUE(comp_sz > 0u);

    /* truncate by one byte — should fail */
    ASSERT_EQ(rgb565_rle_decompress(comp, comp_sz - 1u,
                                    pixels_out, 16u), 0u);

    free(comp);
}

TEST(decompress_wrong_pixel_count_in_header)
{
    uint8_t  comp[] = {
        10u, 0u, 0u, 0u,     /* header: 10 pixels */
        0x80u, 0x34u, 0x12u  /* repeat run of 2: pixel 0x1234 */
    };
    uint16_t pixels[16];

    /*
     * Stream declares 10 pixels but contains only 2 — should fail.
     */
    ASSERT_EQ(rgb565_rle_decompress(comp, sizeof(comp), pixels, 16u), 0u);
}

TEST(decompress_pixel_count_exceeds_capacity)
{
    uint8_t comp[] = {
        100u, 0u, 0u, 0u,   /* header: 100 pixels */
        0x80u, 0x00u, 0x00u /* repeat run, not enough data for 100 */
    };
    uint16_t pixels[16];    /* capacity only 16, but header says 100 */

    ASSERT_EQ(rgb565_rle_decompress(comp, sizeof(comp), pixels, 16u), 0u);
}

TEST(decompress_insufficient_output_capacity)
{
    uint8_t  comp[] = {
        16u, 0u, 0u, 0u,    /* header: 16 pixels */
        /* only data for ~2 pixels */
        0x80u | 15u,        /* repeat run of 16 */
        0x34u, 0x12u
    };
    uint16_t pixels[8];     /* capacity 8, but header says 16 */

    ASSERT_EQ(rgb565_rle_decompress(comp, sizeof(comp), pixels, 8u), 0u);
}

/* --------------------------------------------------------------------------
 * Round-trip: large data set (stress test)
 * -------------------------------------------------------------------------- */

TEST(roundtrip_large)
{
    /*
     * 64 K pixels — exercises the code path with many runs.
     * Use a repeating pattern with some variation to get both
     * literal and repeat runs.
     */
    size_t   count = 65536u;
    uint16_t *pixels;
    size_t   i;
    int      ok;

    pixels = (uint16_t *)malloc(count * sizeof(uint16_t));
    ASSERT_TRUE(pixels != NULL);

    for (i = 0u; i < count; i++) {
        if ((i & 0x3Fu) == 0u) {
            /* every 64 pixels: a small block of identical values */
            pixels[i] = (uint16_t)((i >> 6) & 0xFFFFu);
        } else {
            /* otherwise: gradient-like values */
            pixels[i] = (uint16_t)(i & 0xFFFFu);
        }
    }

    ok = round_trip(pixels, count);
    free(pixels);
    ASSERT_TRUE(ok);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  RGB565 RLE Library — Test Suite\n");
    printf("========================================\n\n");

    /* --- max_compressed_size --- */
    printf("--- max_compressed_size ---\n");
    RUN_TEST(max_size_zero_pixels);
    RUN_TEST(max_size_single_pixel);
    RUN_TEST(max_size_typical);
    RUN_TEST(max_size_overflow);

    /* --- parameter validation --- */
    printf("\n--- parameter validation ---\n");
    RUN_TEST(compress_null_pixels);
    RUN_TEST(compress_null_output);
    RUN_TEST(compress_zero_count);
    RUN_TEST(compress_insufficient_capacity);
    RUN_TEST(decompress_null_input);
    RUN_TEST(decompress_null_output);
    RUN_TEST(decompress_too_small_input);

    /* --- round-trip basics --- */
    printf("\n--- round-trip basics ---\n");
    RUN_TEST(roundtrip_single_pixel);
    RUN_TEST(roundtrip_two_identical);
    RUN_TEST(roundtrip_two_different);
    RUN_TEST(roundtrip_solid_color);
    RUN_TEST(roundtrip_alternating);
    RUN_TEST(roundtrip_gradient);

    /* --- edge cases --- */
    printf("\n--- edge cases (max run length) ---\n");
    RUN_TEST(roundtrip_run_exactly_128);
    RUN_TEST(roundtrip_run_129_same);
    RUN_TEST(roundtrip_literal_exactly_128);

    /* --- corrupted / truncated --- */
    printf("\n--- corrupted / truncated data ---\n");
    RUN_TEST(decompress_truncated_body);
    RUN_TEST(decompress_wrong_pixel_count_in_header);
    RUN_TEST(decompress_pixel_count_exceeds_capacity);
    RUN_TEST(decompress_insufficient_output_capacity);

    /* --- stress --- */
    printf("\n--- stress test ---\n");
    RUN_TEST(roundtrip_large);

    /* --- summary --- */
    printf("\n========================================\n");
    printf("  Results:  %d / %d passed", g_tests_pass, g_tests_run);
    if (g_tests_fail > 0) {
        printf(",  %d FAILED", g_tests_fail);
    }
    printf("\n========================================\n\n");

    return (g_tests_fail == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
