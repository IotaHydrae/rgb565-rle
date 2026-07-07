/*
 * test_benchmark — Performance benchmarks for RGB565 RLE
 *
 * Measures compression and decompression throughput (MB/s, MPixels/s)
 * across a range of buffer sizes and pattern types.
 *
 * Uses clock() — this test requires a hosted C environment.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rgb565_rle.h"

/* ==========================================================================
 * Benchmark harness
 * ========================================================================== */

typedef struct {
    double   compress_time_s;
    size_t   compressed_size;
    double   decompress_time_s;
    size_t   decompressed_pixels;
} bench_result_t;

/*
 * Run one benchmark: compress + decompress, measure both.
 * Returns 0 on error, 1 on success.
 */
static int run_bench(const uint16_t *pixels,
                     size_t pixel_count,
                     int iterations,
                     bench_result_t *out)
{
    size_t   max_sz;
    uint8_t *comp;
    uint16_t *restored;
    clock_t  t0, t1;
    double   total_comp_s   = 0.0;
    double   total_decomp_s = 0.0;
    size_t   comp_sz        = 0u;
    size_t   dec_px         = 0u;
    int      iter;

    max_sz = rgb565_rle_max_compressed_size(pixel_count);
    if (max_sz == 0u) return 0;

    comp     = (uint8_t  *)malloc(max_sz);
    restored = (uint16_t *)malloc(pixel_count * sizeof(uint16_t));
    if ((comp == NULL) || (restored == NULL)) {
        free(comp);
        free(restored);
        return 0;
    }

    for (iter = 0; iter < iterations; iter++) {
        t0 = clock();
        comp_sz = rgb565_rle_compress(pixels, pixel_count, comp, max_sz);
        t1 = clock();
        total_comp_s += (double)(t1 - t0) / (double)CLOCKS_PER_SEC;

        if (comp_sz == 0u) {
            free(restored);
            free(comp);
            return 0;
        }

        t0 = clock();
        dec_px = rgb565_rle_decompress(comp, comp_sz, restored, pixel_count);
        t1 = clock();
        total_decomp_s += (double)(t1 - t0) / (double)CLOCKS_PER_SEC;

        if (dec_px != pixel_count) {
            free(restored);
            free(comp);
            return 0;
        }
    }

    free(restored);
    free(comp);

    out->compress_time_s     = total_comp_s   / (double)iterations;
    out->compressed_size     = comp_sz;
    out->decompress_time_s   = total_decomp_s / (double)iterations;
    out->decompressed_pixels = dec_px;

    return 1;
}

static void print_row(const char *label,
                      size_t px,
                      double comp_s,
                      size_t comp_sz,
                      double decomp_s)
{
    double raw_mb   = (double)(px * 2u) / (1024.0 * 1024.0);
    double comp_mbs;
    double decomp_mbs;
    double comp_mpx_s;
    double decomp_mpx_s;

    comp_mbs   = (comp_s  > 0.0) ? raw_mb / comp_s   : 0.0;
    decomp_mbs = (decomp_s > 0.0) ? raw_mb / decomp_s : 0.0;

    comp_mpx_s   = (comp_s  > 0.0) ? (double)px / comp_s   / 1e6 : 0.0;
    decomp_mpx_s = (decomp_s > 0.0) ? (double)px / decomp_s / 1e6 : 0.0;

    printf("  %-20s  %8zu px  ", label, px);
    printf("%8.2f MB/s (%6.1f MPx/s)  ",
           comp_mbs, comp_mpx_s);
    printf("%8.2f MB/s (%6.1f MPx/s)  ",
           decomp_mbs, decomp_mpx_s);
    printf("%6zu B (%5.1f%%)",
           comp_sz,
           (double)comp_sz / (double)(px * 2u) * 100.0);
    printf("\n");
}

/* ==========================================================================
 * Pattern generators
 * ========================================================================== */

static void fill_solid(uint16_t *p, size_t n, uint16_t c)
{
    size_t i;
    for (i = 0u; i < n; i++) p[i] = c;
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

static void fill_noise(uint16_t *p, size_t n)
{
    size_t  i;
    uint32_t seed = 0x12345678u;
    for (i = 0u; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (uint16_t)((seed >> 16) & 0xFFFFu);
    }
}

static void fill_mixed(uint16_t *p, size_t n)
{
    /* 60% gradient-like, 40% small repeat blocks — somewhat "real world" */
    size_t i;
    for (i = 0u; i < n; i++) {
        if ((i & 0x1Fu) < 12u) {
            /* small repeat blocks */
            p[i] = (uint16_t)((i >> 5) & 0xFFFFu);
        } else {
            /* gradient-like variation */
            uint16_t r = (uint16_t)((i * 7u) % 32u) & 0x1Fu;
            uint16_t g = (uint16_t)((i * 13u) % 64u) & 0x3Fu;
            uint16_t b = (uint16_t)((i * 3u) % 32u) & 0x1Fu;
            p[i] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    size_t   sizes[]    = { 1024u, 16384u, 65536u, 262144u, 1048576u };
    size_t   num_sizes  = sizeof(sizes) / sizeof(sizes[0]);
    size_t   si;
    int      iterations = 5;
    uint16_t *pixels    = NULL;
    size_t   max_px     = sizes[num_sizes - 1u];

    printf("\n");
    printf("==============================================================\n");
    printf("  RGB565 RLE — Performance Benchmark\n");
    printf("  %d iterations per test, best-case timing reported\n", iterations);
    printf("==============================================================\n");

    pixels = (uint16_t *)malloc(max_px * sizeof(uint16_t));
    if (pixels == NULL) {
        fprintf(stderr, "Allocation failed for %zu pixels.\n", max_px);
        return EXIT_FAILURE;
    }

    /* ---- solid colour ---- */
    printf("\n--- Pattern: solid white (best case) ---\n\n");
    printf("  %-20s  %8s  %-26s  %-26s  %s\n",
           "pattern", "pixels",
           "compress", "decompress", "size");
    printf("  %-20s  %8s  %-26s  %-26s  %s\n",
           "-------------------", "--------",
           "-------------------------", "-------------------------", "----");

    for (si = 0u; si < num_sizes; si++) {
        bench_result_t r;
        size_t n = sizes[si];
        fill_solid(pixels, n, 0xFFFFu);
        if (run_bench(pixels, n, iterations, &r)) {
            char label[32];
            snprintf(label, sizeof(label), "solid %zu", n);
            print_row(label, n,
                      r.compress_time_s, r.compressed_size,
                      r.decompress_time_s);
        }
    }

    /* ---- gradient ---- */
    printf("\n--- Pattern: gradient (mostly literal) ---\n\n");
    printf("  %-20s  %8s  %-26s  %-26s  %s\n",
           "pattern", "pixels",
           "compress", "decompress", "size");
    printf("  %-20s  %8s  %-26s  %-26s  %s\n",
           "-------------------", "--------",
           "-------------------------", "-------------------------", "----");

    for (si = 0u; si < num_sizes; si++) {
        bench_result_t r;
        size_t n = sizes[si];
        fill_gradient(pixels, n);
        if (run_bench(pixels, n, iterations, &r)) {
            char label[32];
            snprintf(label, sizeof(label), "gradient %zu", n);
            print_row(label, n,
                      r.compress_time_s, r.compressed_size,
                      r.decompress_time_s);
        }
    }

    /* ---- noise ---- */
    printf("\n--- Pattern: pseudo-random noise (worst case) ---\n\n");
    printf("  %-20s  %8s  %-26s  %-26s  %s\n",
           "pattern", "pixels",
           "compress", "decompress", "size");
    printf("  %-20s  %8s  %-26s  %-26s  %s\n",
           "-------------------", "--------",
           "-------------------------", "-------------------------", "----");

    for (si = 0u; si < num_sizes; si++) {
        bench_result_t r;
        size_t n = sizes[si];
        fill_noise(pixels, n);
        if (run_bench(pixels, n, iterations, &r)) {
            char label[32];
            snprintf(label, sizeof(label), "noise %zu", n);
            print_row(label, n,
                      r.compress_time_s, r.compressed_size,
                      r.decompress_time_s);
        }
    }

    /* ---- mixed ---- */
    printf("\n--- Pattern: mixed (simulated real-world) ---\n\n");
    printf("  %-20s  %8s  %-26s  %-26s  %s\n",
           "pattern", "pixels",
           "compress", "decompress", "size");
    printf("  %-20s  %8s  %-26s  %-26s  %s\n",
           "-------------------", "--------",
           "-------------------------", "-------------------------", "----");

    for (si = 0u; si < num_sizes; si++) {
        bench_result_t r;
        size_t n = sizes[si];
        fill_mixed(pixels, n);
        if (run_bench(pixels, n, iterations, &r)) {
            char label[32];
            snprintf(label, sizeof(label), "mixed %zu", n);
            print_row(label, n,
                      r.compress_time_s, r.compressed_size,
                      r.decompress_time_s);
        }
    }

    free(pixels);

    printf("\n--- notes ---\n");
    printf("  clock() resolution: %zu ticks/sec\n",
           (size_t)CLOCKS_PER_SEC);
    printf("  Decompress throughput uses raw-pixel throughput"
           " (decompressed data rate).\n");
    printf("\nDone.\n\n");

    return EXIT_SUCCESS;
}
