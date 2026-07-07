/*
 * test_compression_ratio — Analyse compression ratios across patterns
 *
 * Tests how well the RLE encoder handles various pixel patterns:
 * solid fills, gradients, alternating, blocks, checkerboard, noise.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rgb565_rle.h"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

#define TEST_SIZE 1024u

static void fill_solid(uint16_t *p, size_t n, uint16_t color)
{
    size_t i;
    for (i = 0u; i < n; i++) p[i] = color;
}

static void fill_gradient(uint16_t *p, size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        uint16_t r = (uint16_t)((i * 31u) / n) & 0x1Fu;
        uint16_t g = (uint16_t)((i * 63u) / n) & 0x3Fu;
        uint16_t b = (uint16_t)((i * 31u) / n) & 0x1Fu;
        p[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

static void fill_alternating(uint16_t *p, size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++)
        p[i] = (i & 1u) ? 0xF800u : 0x001Fu;  /* red / blue */
}

static void fill_checkerboard(uint16_t *p, size_t n, size_t width)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        size_t x = i % width;
        size_t y = i / width;
        p[i] = ((x ^ y) & 1u) ? 0xFFFFu : 0x0000u;  /* white / black */
    }
}

static void fill_stripes(uint16_t *p, size_t n, size_t stripe_w)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        size_t block = (i / stripe_w) & 0x3u;
        switch (block) {
            case 0u: p[i] = 0xF800u; break;  /* red    */
            case 1u: p[i] = 0x07E0u; break;  /* green  */
            case 2u: p[i] = 0x001Fu; break;  /* blue   */
            case 3u: p[i] = 0xFFE0u; break;  /* yellow */
        }
    }
}

static void fill_blocks(uint16_t *p, size_t n, size_t block_w, size_t width)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        size_t bx = (i % width) / block_w;
        size_t by = (i / width) / block_w;
        uint16_t r = (uint16_t)(bx * 7u) & 0x1Fu;
        uint16_t g = (uint16_t)(by * 15u) & 0x3Fu;
        uint16_t b = (uint16_t)((bx + by) * 7u) & 0x1Fu;
        p[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

static void fill_noise(uint16_t *p, size_t n)
{
    size_t i;
    uint32_t seed = 0xDEADBEEFu;
    for (i = 0u; i < n; i++) {
        /* simple LCG */
        seed = seed * 1103515245u + 12345u;
        p[i] = (uint16_t)((seed >> 16) & 0xFFFFu);
    }
}

static void test_pattern(const char *name, const uint16_t *pixels, size_t n)
{
    size_t  max_sz;
    uint8_t *comp;
    size_t  comp_sz;
    double  ratio;
    double  raw_bytes;

    raw_bytes = (double)(n * 2u);
    max_sz    = rgb565_rle_max_compressed_size(n);

    if (max_sz == 0u) {
        printf("  %-28s  SKIP (overflow)\n", name);
        return;
    }

    comp = (uint8_t *)malloc(max_sz);
    if (comp == NULL) {
        printf("  %-28s  SKIP (OOM)\n", name);
        return;
    }

    comp_sz = rgb565_rle_compress(pixels, n, comp, max_sz);
    if (comp_sz == 0u) {
        printf("  %-28s  FAIL\n", name);
        free(comp);
        return;
    }

    ratio = (double)comp_sz / raw_bytes * 100.0;

    printf("  %-28s  %6zu B → %6zu B  (%5.1f%% of raw)\n",
           name, (size_t)raw_bytes, comp_sz, ratio);

    free(comp);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    uint16_t *pixels;

    printf("\n");
    printf("========================================\n");
    printf("  RGB565 RLE — Compression Ratio Tests\n");
    printf("========================================\n");

    pixels = (uint16_t *)malloc(TEST_SIZE * sizeof(uint16_t));
    if (pixels == NULL) {
        fprintf(stderr, "Allocation failed.\n");
        return EXIT_FAILURE;
    }

    /* ---- 1024 pixels, various patterns ---- */
    printf("\n--- %u pixels ---\n\n", (unsigned)TEST_SIZE);

    fill_solid(pixels, TEST_SIZE, 0xFFFFu);
    test_pattern("solid white", pixels, TEST_SIZE);

    fill_solid(pixels, TEST_SIZE, 0xF800u);
    test_pattern("solid red", pixels, TEST_SIZE);

    fill_gradient(pixels, TEST_SIZE);
    test_pattern("gradient", pixels, TEST_SIZE);

    fill_alternating(pixels, TEST_SIZE);
    test_pattern("alternating (R/B)", pixels, TEST_SIZE);

    fill_checkerboard(pixels, TEST_SIZE, 32u);
    test_pattern("checkerboard 32px", pixels, TEST_SIZE);

    fill_checkerboard(pixels, TEST_SIZE, 8u);
    test_pattern("checkerboard 8px", pixels, TEST_SIZE);

    fill_stripes(pixels, TEST_SIZE, 64u);
    test_pattern("stripes 64px", pixels, TEST_SIZE);

    fill_stripes(pixels, TEST_SIZE, 16u);
    test_pattern("stripes 16px", pixels, TEST_SIZE);

    fill_blocks(pixels, TEST_SIZE, 32u, 32u);
    test_pattern("blocks 32x32", pixels, TEST_SIZE);

    fill_noise(pixels, TEST_SIZE);
    test_pattern("pseudo-random noise", pixels, TEST_SIZE);

    /* ---- various sizes, solid ---- */
    printf("\n--- solid white, various sizes ---\n\n");

    {
        size_t sizes[] = { 64u, 128u, 256u, 512u, 1024u, 4096u, 16384u };
        size_t ns = sizeof(sizes) / sizeof(sizes[0]);
        size_t si;

        for (si = 0u; si < ns; si++) {
            size_t n = sizes[si];
            uint16_t *buf = (uint16_t *)malloc(n * sizeof(uint16_t));
            char label[64];
            if (buf == NULL) continue;
            fill_solid(buf, n, 0xFFFFu);
            snprintf(label, sizeof(label), "solid %zu px", n);
            test_pattern(label, buf, n);
            free(buf);
        }
    }

    /* ---- various sizes, noise ---- */
    printf("\n--- pseudo-random noise, various sizes ---\n\n");

    {
        size_t sizes[] = { 64u, 128u, 256u, 512u, 1024u, 4096u, 16384u };
        size_t ns = sizeof(sizes) / sizeof(sizes[0]);
        size_t si;

        for (si = 0u; si < ns; si++) {
            size_t n = sizes[si];
            uint16_t *buf = (uint16_t *)malloc(n * sizeof(uint16_t));
            char label[64];
            if (buf == NULL) continue;
            fill_noise(buf, n);
            snprintf(label, sizeof(label), "noise %zu px", n);
            test_pattern(label, buf, n);
            free(buf);
        }
    }

    /* ---- worst-case analysis ---- */
    printf("\n--- worst-case bound vs actual ---\n\n");

    {
        size_t  sizes_bc[] = { 1u, 16u, 64u, 128u, 256u, 1024u, 65536u };
        size_t  ns = sizeof(sizes_bc) / sizeof(sizes_bc[0]);
        size_t  si;

        for (si = 0u; si < ns; si++) {
            size_t n    = sizes_bc[si];
            size_t max  = rgb565_rle_max_compressed_size(n);
            size_t raw  = n * 2u;
            printf("  %6zu px:  raw=%6zu B  max_compressed=%6zu B  "
                   "worst_ratio=%.1f%%\n",
                   n, raw, max,
                   (raw > 0u) ? (double)max / (double)raw * 100.0 : 0.0);
        }
    }

    free(pixels);
    printf("\nDone.\n\n");
    return EXIT_SUCCESS;
}
