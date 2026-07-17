/*
 * test_benchmark — Performance benchmarks for RGB565 RLE
 *
 * Measures compression and decompression throughput (MB/s, MPixels/s)
 * across buffer sizes, pattern types, and API variants:
 *   - full-buffer decompress (rgb565_rle_decompress)
 *   - callback single-buffer (buf_b = NULL)
 *   - callback ping-pong   (buf_b != NULL)
 *
 * Parts 1-5 use a no-op callback to measure pure decode throughput.
 * Part 6 uses a simulated blocking/non-blocking DMA callback to
 * demonstrate the ping-pong overlap advantage.
 *
 * Uses clock_gettime(CLOCK_MONOTONIC) for nanosecond-resolution timing.
 * Requires a hosted C environment (POSIX.1-1993 or later).
 *
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 199309L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rgb565_rle.h"

/* ==========================================================================
 * Timing helpers
 * ========================================================================== */

static double time_diff_s(const struct timespec *start,
                          const struct timespec *end)
{
    return (double)(end->tv_sec  - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1e9;
}

typedef struct {
    double min_s;
    double avg_s;
    double max_s;
} timing_t;

/*
 * Run a measured operation `iterations` times (after one warmup).
 * Fills `t` with min / avg / max wall-clock seconds.
 */
#define MEASURE(iterations, setup, body, t)                         \
    do {                                                             \
        int _i;                                                      \
        double _sum = 0.0;                                           \
        (t).min_s = 1e99;                                            \
        (t).max_s = 0.0;                                             \
        /* warmup */                                                 \
        { setup; body; }                                             \
        for (_i = 0; _i < (iterations); _i++) {                      \
            struct timespec _t0, _t1;                                \
            { setup; }                                               \
            clock_gettime(CLOCK_MONOTONIC, &_t0);                    \
            { body; }                                                \
            clock_gettime(CLOCK_MONOTONIC, &_t1);                    \
            double _elapsed = time_diff_s(&_t0, &_t1);               \
            if (_elapsed < (t).min_s) (t).min_s = _elapsed;          \
            if (_elapsed > (t).max_s) (t).max_s = _elapsed;          \
            _sum += _elapsed;                                        \
        }                                                            \
        (t).avg_s = _sum / (double)(iterations);                     \
    } while (0)

/* ==========================================================================
 * Benchmark parameters
 * ========================================================================== */

#define ITERATIONS 10
#define WARMUP_ITERS 2

/* Sizes in pixels: 1K, 16K, 64K, 256K, 1M */
static const size_t sizes[] = {
    1024u, 16384u, 65536u, 262144u, 1048576u
};
static const size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);

/* ==========================================================================
 * Pattern generators
 * ========================================================================== */

static void fill_solid(uint16_t *p, size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++) p[i] = 0xFFFFu; /* white */
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
    size_t   i;
    uint32_t seed = 0x12345678u;
    for (i = 0u; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (uint16_t)((seed >> 16) & 0xFFFFu);
    }
}

static void fill_mixed(uint16_t *p, size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        if ((i & 0x1Fu) < 12u) {
            p[i] = (uint16_t)((i >> 5) & 0xFFFFu);
        } else {
            uint16_t r = (uint16_t)((i * 7u) % 32u) & 0x1Fu;
            uint16_t g = (uint16_t)((i * 13u) % 64u) & 0x3Fu;
            uint16_t b = (uint16_t)((i * 3u) % 32u) & 0x1Fu;
            p[i] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

typedef void (*fill_fn)(uint16_t *p, size_t n);

static const char *pattern_name(fill_fn fn)
{
    if (fn == fill_solid)    return "solid";
    if (fn == fill_gradient) return "gradient";
    if (fn == fill_noise)    return "noise";
    if (fn == fill_mixed)    return "mixed";
    return "?";
}

/* ==========================================================================
 * Callback context — counts calls and pixels, does no I/O
 * ========================================================================== */

typedef struct {
    size_t call_count;
    size_t total_pixels;
    int    toggle_count;   /* how many times buffer switched */
    uint16_t *last_buf;    /* to detect ping-pong toggles */
} cb_stats_t;

static void cb_collect(const uint16_t *pixels, size_t count,
                       uint16_t xs, uint16_t ys,
                       uint16_t xe, uint16_t ye,
                       void *user_data)
{
    cb_stats_t *s = (cb_stats_t *)user_data;

    s->call_count++;
    s->total_pixels += count;

    if (s->last_buf != NULL && pixels != s->last_buf) {
        s->toggle_count++;
    }
    s->last_buf = (uint16_t *)pixels;

    (void)xs; (void)ys; (void)xe; (void)ye;
}

/* ==========================================================================
 * Simulated-DMA overlap callback
 *
 * Models the key difference between single-buffer and ping-pong:
 *
 *   SINGLE-BUFFER: the library reuses buf_a after the callback returns.
 *   The callback MUST wait for DMA to finish → blocking.
 *   total frame time = decode_time + sum(each DMA transfer time)
 *
 *   PING-PONG: the library fills buf_b while DMA runs on buf_a.
 *   The callback does NOT need to wait → non-blocking.
 *   total frame time ≈ max(decode_time, sum(all DMA transfer time))
 *
 * We simulate this with busy-wait spin loops.  For ping-pong the
 * wait is deferred — after decompress returns we spin for any
 * remaining DMA time, modelling the overlap.
 * ========================================================================== */

typedef struct {
    double bytes_per_sec;   /* simulated bus speed (0 = instant) */
    int    defer_dma;       /* 1 = non-blocking (ping-pong), 0 = blocking */
    double pending_dma_s;   /* accumulated DMA time not yet waited for */
    size_t total_bytes;     /* total bytes transferred */
    size_t call_count;      /* callback invocation count */
    double dma_time_s;      /* total theoretical DMA time */
} dma_overlap_t;

/*
 * Spin until `target_s` seconds have elapsed since `t0`.
 */
static void spin_until(const struct timespec *t0, double target_s)
{
    struct timespec now;
    double elapsed;

    if (target_s <= 0.0) return;

    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed = time_diff_s(t0, &now);
        if (elapsed >= target_s) break;
    }
}

static void cb_dma_overlap(const uint16_t *pixels, size_t count,
                           uint16_t xs, uint16_t ys,
                           uint16_t xe, uint16_t ye,
                           void *user_data)
{
    dma_overlap_t *ds = (dma_overlap_t *)user_data;
    size_t         byte_count = count * sizeof(uint16_t);
    double         dma_s;

    (void)xs; (void)ys; (void)xe; (void)ye;
    (void)pixels;

    /* how long SHOULD this DMA transfer take? */
    dma_s = (ds->bytes_per_sec > 0.0)
                ? (double)byte_count / ds->bytes_per_sec
                : 0.0;

    ds->total_bytes += byte_count;
    ds->call_count++;
    ds->dma_time_s += dma_s;

    if (ds->defer_dma) {
        /*
         * PING-PONG: DMA runs in background (async controller).
         * Don't block — just accumulate pending time.
         * The library fills the OTHER buffer while DMA is busy.
         */
        ds->pending_dma_s += dma_s;
    } else {
        /*
         * SINGLE-BUFFER: MUST wait.  If we return now the library
         * will overwrite this buffer with new pixel data, corrupting
         * the DMA in progress.
         */
        double wait_s = ds->pending_dma_s + dma_s;
        if (wait_s > 0.0) {
            struct timespec t0;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            spin_until(&t0, wait_s);
        }
        ds->pending_dma_s = 0.0;
    }
}

/* ==========================================================================
 * Print helpers
 * ========================================================================== */

static double raw_mb(size_t px)
{
    return (double)(px * 2u) / (1024.0 * 1024.0);
}

/* throughput formatting */
static void fmt_tp(double sec, size_t px,
                   char *mbuf, size_t mbuflen,
                   char *pxbuf, size_t pxbuflen)
{
    double raw = raw_mb(px);
    if (sec > 0.0) {
        snprintf(mbuf,  mbuflen,  "%7.1f MB/s",  raw / sec);
        snprintf(pxbuf, pxbuflen, "%6.1f MPx/s", (double)px / sec / 1e6);
    } else {
        snprintf(mbuf,  mbuflen,  "%7s",  "---");
        snprintf(pxbuf, pxbuflen, "%6s",  "---");
    }
}

/* ratio %: compressed / raw * 100 */
static double ratio_pct(size_t comp_sz, size_t px)
{
    return (double)comp_sz / (double)(px * 2u) * 100.0;
}

/* ==========================================================================
 * RLE statistics — count runs in a compressed stream
 * ========================================================================== */

typedef struct {
    size_t literal_runs;
    size_t repeat_runs;
    size_t total_runs;
    size_t compressed_size;
} rle_stats_t;

static rle_stats_t analyze_rle(const uint8_t *data, size_t data_size)
{
    rle_stats_t st;
    size_t pos;

    memset(&st, 0, sizeof(st));
    st.compressed_size = data_size;

    if (data_size < 4u) return st;

    pos = 4u; /* skip header */

    while (pos < data_size) {
        uint8_t ctl = data[pos++];
        size_t  run_len = ((size_t)(ctl & 0x7Fu)) + 1u;

        if (ctl & 0x80u) {
            st.repeat_runs++;
            pos += 2u; /* one pixel */
        } else {
            st.literal_runs++;
            pos += run_len * 2u;
        }
        st.total_runs++;
    }

    return st;
}

/* ==========================================================================
 * Compress benchmark
 * ========================================================================== */

typedef struct {
    size_t     pixel_count;
    size_t     compressed_size;
    timing_t   timing;
    rle_stats_t rle;
} comp_result_t;

static int bench_compress(const uint16_t *pixels, size_t pixel_count,
                          uint8_t *comp_out, size_t comp_capacity,
                          comp_result_t *out)
{
    size_t cs;

    if (comp_out == NULL || comp_capacity == 0u) return 0;

    /* warmup */
    cs = rgb565_rle_compress(pixels, pixel_count, comp_out, comp_capacity);
    if (cs == 0u) return 0;

    /* measure */
    MEASURE(ITERATIONS,
        /* setup */ (void)0,
        /* body */  cs = rgb565_rle_compress(pixels, pixel_count,
                                             comp_out, comp_capacity),
        out->timing);

    if (cs == 0u) return 0;

    out->pixel_count     = pixel_count;
    out->compressed_size = cs;
    out->rle             = analyze_rle(comp_out, cs);

    return 1;
}

/* ==========================================================================
 * Decompress benchmarks
 * ========================================================================== */

typedef struct {
    size_t     pixel_count;
    timing_t   timing;
    size_t     callback_calls;
    size_t     buffer_toggles;
} decomp_result_t;

/* full-buffer decompress */
static int bench_decompress_full(const uint8_t *comp, size_t comp_size,
                                 size_t pixel_count, decomp_result_t *out)
{
    uint16_t *pixels;
    size_t    dp;

    pixels = (uint16_t *)malloc(pixel_count * sizeof(uint16_t));
    if (pixels == NULL) return 0;

    MEASURE(ITERATIONS,
        (void)0,
        dp = rgb565_rle_decompress(comp, comp_size, pixels, pixel_count),
        out->timing);

    free(pixels);

    if (dp != pixel_count) return 0;

    out->pixel_count    = pixel_count;
    out->callback_calls = 0;
    out->buffer_toggles = 0;
    return 1;
}

/* callback decompress (single or ping-pong) */
static int bench_decompress_cb(const uint8_t *comp, size_t comp_size,
                               uint16_t width,
                               uint16_t *buf_a, uint16_t *buf_b,
                               size_t buf_capacity,
                               decomp_result_t *out)
{
    cb_stats_t cb_st;
    size_t     dp;

    MEASURE(ITERATIONS,
        memset(&cb_st, 0, sizeof(cb_st)),
        dp = rgb565_rle_decompress_callback(comp, comp_size,
                                             width,
                                             buf_a, buf_b, buf_capacity,
                                             cb_collect, &cb_st),
        out->timing);

    if (dp == 0u) return 0;

    out->pixel_count    = dp;
    out->callback_calls = cb_st.call_count;
    out->buffer_toggles = cb_st.toggle_count;
    return 1;
}

/* ==========================================================================
 * Section output helpers
 * ========================================================================== */

static void print_sep(void)
{
    printf("  %s\n", "-----------------------------------------------------------"
           "----------------------------------------------");
}

static void print_header(const char *title)
{
    printf("\n");
    print_sep();
    printf("  %s\n", title);
    print_sep();
}

/*
 * Print a compress + full-decompress row.
 */
static void print_compress_row(const char *label,
                               const comp_result_t *cr,
                               const decomp_result_t *dr)
{
    char c_mb[16], c_px[16], d_mb[16], d_px[16];

    fmt_tp(cr->timing.avg_s, cr->pixel_count, c_mb, sizeof(c_mb),
           c_px, sizeof(c_px));
    fmt_tp(dr->timing.avg_s, dr->pixel_count, d_mb, sizeof(d_mb),
           d_px, sizeof(d_px));

    printf("  %-22s %8zu px  %s (%s)  %s (%s)  %6zu B (%5.1f%%)  "
           "R=%zu L=%zu\n",
           label, cr->pixel_count,
           c_mb, c_px, d_mb, d_px,
           cr->compressed_size,
           ratio_pct(cr->compressed_size, cr->pixel_count),
           cr->rle.repeat_runs, cr->rle.literal_runs);
}

/*
 * Print a decompress comparison row (full vs callback variants).
 */
static void print_decomp_compare_row(const char *label, size_t px,
                                     const decomp_result_t *full,
                                     const decomp_result_t *cb1,
                                     size_t cb1_buf_px,
                                     const decomp_result_t *cb2,
                                     size_t cb2_buf_px,
                                     size_t compressed_size)
{
    char f_mb[16], f_px[16];
    char c1_mb[16], c1_px[16];
    char c2_mb[16], c2_px[16];

    fmt_tp(full->timing.avg_s,  px, f_mb,  sizeof(f_mb),  f_px,  sizeof(f_px));
    fmt_tp(cb1->timing.avg_s,   px, c1_mb, sizeof(c1_mb), c1_px, sizeof(c1_px));
    fmt_tp(cb2->timing.avg_s,   px, c2_mb, sizeof(c2_mb), c2_px, sizeof(c2_px));

    printf("  %-22s %6zu px  "
           "%s (%s)          "
           "%s (%s) %5zux%zu "
           "%s (%s) %5zux%zu "
           "%5.1f%%  %4zu/%zu calls\n",
           label, px,
           f_mb, f_px,
           c1_mb, c1_px,
           cb1_buf_px, px / cb1_buf_px + (px % cb1_buf_px ? 1u : 0u),
           c2_mb, c2_px,
           cb2_buf_px, px / cb2_buf_px + (px % cb2_buf_px ? 1u : 0u),
           ratio_pct(compressed_size, px),
           cb1->callback_calls, cb2->callback_calls);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    size_t   si;
    uint16_t *pixels    = NULL;
    uint8_t  *comp_data = NULL;
    size_t   max_px;
    size_t   max_cs;
    fill_fn  patterns[] = { fill_solid, fill_gradient, fill_noise, fill_mixed };
    size_t   num_pat    = sizeof(patterns) / sizeof(patterns[0]);
    size_t   pi;

    /* commonly used display widths for context */
    const uint16_t disp_w[] = { 240u, 320u, 480u, 800u };
    const size_t   num_dw   = sizeof(disp_w) / sizeof(disp_w[0]);

    max_px = sizes[num_sizes - 1u];
    max_cs = rgb565_rle_max_compressed_size(max_px);

    printf("\n");
    printf("==============================================================\n");
    printf("  RGB565 RLE — Performance Benchmark\n");
    printf("  %d measured iterations (+ %d warmup), clock_gettime(MONOTONIC)\n",
           ITERATIONS, WARMUP_ITERS);
    printf("==============================================================\n");

    printf("\n  Sizes tested:");
    for (si = 0u; si < num_sizes; si++) {
        printf(" %zu (%.0f KB)", sizes[si], (double)sizes[si] * 2.0 / 1024.0);
    }
    printf("\n");

    /* alloc once */
    pixels    = (uint16_t *)malloc(max_px * sizeof(uint16_t));
    comp_data = (uint8_t  *)malloc(max_cs);
    if ((pixels == NULL) || (comp_data == NULL)) {
        fprintf(stderr, "Allocation failed.\n");
        free(pixels);
        free(comp_data);
        return EXIT_FAILURE;
    }

    /* ======================================================================
     * PART 1 — Compress + Full Decompress (all patterns, all sizes)
     * ====================================================================== */

    print_header("PART 1 — Compress & Full-Buffer Decompress Throughput");

    printf("  %-22s  %8s  %-24s  %-24s  %-8s  %s\n",
           "pattern", "pixels",
           "compress", "decompress (full buf)",
           "comp_sz", "runs (R=repeat L=literal)");
    printf("  %-22s  %8s  %-24s  %-24s  %-8s  %s\n",
           "----------------------", "--------",
           "-----------------------", "-----------------------",
           "--------", "------------------------");

    for (pi = 0u; pi < num_pat; pi++) {
        if (pi > 0u) printf("\n");
        for (si = 0u; si < num_sizes; si++) {
            comp_result_t  cr;
            decomp_result_t dr;
            size_t n = sizes[si];

            patterns[pi](pixels, n);

            if (!bench_compress(pixels, n, comp_data, max_cs, &cr)) {
                printf("  %-22s %8zu px  COMPRESS FAILED\n",
                       pattern_name(patterns[pi]), n);
                continue;
            }

            if (!bench_decompress_full(comp_data, cr.compressed_size,
                                       n, &dr)) {
                printf("  %-22s %8zu px  DECOMPRESS FAILED\n",
                       pattern_name(patterns[pi]), n);
                continue;
            }

            {
                char label[40];
                snprintf(label, sizeof(label), "%s",
                         pattern_name(patterns[pi]));
                print_compress_row(label, &cr, &dr);
            }
        }
    }

    /* ======================================================================
     * PART 2 — Decompress API Comparison (full vs callback-single vs cb-pingpong)
     * ====================================================================== */

    print_header("PART 2 — Decompress API Comparison: Full vs Callback");

    printf("  Comparing three decode paths on the same compressed data.\n");
    printf("  cb-1buf = callback single-buffer (1 row = %u px)\n", disp_w[0]);
    printf("  cb-2buf = callback ping-pong   (1 row = %u px)\n", disp_w[0]);
    printf("\n");
    printf("  %-22s %6s  %-22s  %-30s  %-30s  %6s  %s\n",
           "pattern", "pixels",
           "full-buf", "cb-1buf (1 row)", "cb-2buf (1 row)",
           "ratio", "calls (1buf/2buf)");
    printf("  %-22s %6s  %-22s  %-30s  %-30s  %6s  %s\n",
           "----------------------", "------",
           "---------------------", "-----------------------------",
           "-----------------------------", "------", "------------------");

    for (pi = 0u; pi < num_pat; pi++) {
        if (pi > 0u) printf("\n");
        for (si = 0u; si < num_sizes; si++) {
            decomp_result_t full, cb1, cb2;
            size_t          n  = sizes[si];
            size_t          cs;
            uint16_t       *ba, *bb;
            uint16_t        width = disp_w[0]; /* simulate 240px-wide display */
            size_t           buf_px = width;   /* 1 row */

            patterns[pi](pixels, n);

            /* compress once */
            cs = rgb565_rle_compress(pixels, n, comp_data, max_cs);
            if (cs == 0u) { printf("  compress failed\n"); continue; }

            /* full */
            if (!bench_decompress_full(comp_data, cs, n, &full)) continue;

            /* callback single-buffer */
            ba = (uint16_t *)malloc(buf_px * sizeof(uint16_t));
            if (ba == NULL) continue;
            if (!bench_decompress_cb(comp_data, cs, (uint16_t)width,
                                      ba, NULL, buf_px, &cb1)) {
                free(ba); continue;
            }
            free(ba);

            /* callback ping-pong */
            ba = (uint16_t *)malloc(buf_px * sizeof(uint16_t));
            bb = (uint16_t *)malloc(buf_px * sizeof(uint16_t));
            if ((ba == NULL) || (bb == NULL)) {
                free(ba); free(bb); continue;
            }
            if (!bench_decompress_cb(comp_data, cs, (uint16_t)width,
                                      ba, bb, buf_px, &cb2)) {
                free(ba); free(bb); continue;
            }
            free(ba); free(bb);

            {
                char label[40];
                snprintf(label, sizeof(label), "%s",
                         pattern_name(patterns[pi]));
                print_decomp_compare_row(label, n,
                                         &full, &cb1, buf_px,
                                         &cb2, buf_px, cs);
            }
        }
    }

    /* ======================================================================
     * PART 3 — Buffer Capacity Sweep (callback only)
     * ====================================================================== */

    print_header("PART 3 — Callback Throughput vs Buffer Capacity");

    printf("  How buffer size affects callback frequency and throughput.\n");
    printf("  Pattern: mixed (most realistic), ");
    printf("%zu px (%.0f KB raw).\n",
           sizes[3], (double)sizes[3] * 2.0 / 1024.0);
    printf("\n");

    {
        size_t     n  = sizes[3]; /* 256K px */
        size_t     cs;
        uint16_t  *ba, *bb;
        size_t     bi;

        /*
         * buf capacities to test: 1/4 row through full-buffer.
         * We use "display widths" as row sizes for realistic context.
         */
        size_t cap_sweep[] = {
            60u,     /* 1/4 row @ 240px */
            120u,    /* 1/2 row */
            240u,    /* 1 row */
            480u,    /* 2 rows */
            1200u,   /* 5 rows */
            2400u,   /* 10 rows */
            4800u,   /* 20 rows */
            24000u,  /* 100 rows */
            n        /* full buffer */
        };
        size_t num_caps = sizeof(cap_sweep) / sizeof(cap_sweep[0]);

        fill_mixed(pixels, n);
        cs = rgb565_rle_compress(pixels, n, comp_data, max_cs);
        if (cs == 0u) {
            printf("  compress failed.\n");
            goto done;
        }

        printf("  %-22s  %-24s  %-24s  %s\n",
               "buffer (pixels)", "single-buffer", "ping-pong", "calls / toggles");
        printf("  %-22s  %-24s  %-24s  %s\n",
               "---------------------", "-----------------------",
               "-----------------------", "----------------");

        for (bi = 0u; bi < num_caps; bi++) {
            decomp_result_t r1, r2;
            size_t cap = cap_sweep[bi];

            ba = (uint16_t *)malloc(cap * sizeof(uint16_t));
            if (ba == NULL) continue;

            /* use a fixed display width — width only affects
               coordinate reporting, not decode throughput */
            memset(&r1, 0, sizeof(r1));
            bench_decompress_cb(comp_data, cs, 320u,
                                ba, NULL, cap, &r1);

            /* ping-pong */
            bb = (uint16_t *)malloc(cap * sizeof(uint16_t));
            if (bb != NULL) {
                bench_decompress_cb(comp_data, cs, 320u,
                                    ba, bb, cap, &r2);
                free(bb);
            } else {
                memset(&r2, 0, sizeof(r2));
            }

            {
                char m1[16], p1[16], m2[16], p2[16];
                fmt_tp(r1.timing.avg_s, n, m1, sizeof(m1), p1, sizeof(p1));
                fmt_tp(r2.timing.avg_s, n, m2, sizeof(m2), p2, sizeof(p2));

                printf("  %6zu px (%5.1f KB)      "
                       "%s (%s) %4zu calls  "
                       "%s (%s) %4zu calls  "
                       "%zu tog\n",
                       cap, (double)cap * 2.0 / 1024.0,
                       m1, p1, r1.callback_calls,
                       m2, p2, r2.callback_calls,
                       r2.buffer_toggles);
            }

            free(ba);
        }
    }

    /* ======================================================================
     * PART 4 — Display Simulation (different widths)
     * ====================================================================== */

    print_header("PART 4 — Simulated Display: Callback vs Width");

    printf("  Single-buffer callback, 10-row capacity per width.\n");
    printf("  Pattern: mixed, %zu px (%.0f KB raw).\n",
           sizes[3], (double)sizes[3] * 2.0 / 1024.0);
    printf("\n");

    {
        size_t    n  = sizes[3];
        size_t    cs;
        size_t    di;

        fill_mixed(pixels, n);
        cs = rgb565_rle_compress(pixels, n, comp_data, max_cs);
        if (cs == 0u) {
            printf("  compress failed.\n");
            goto done;
        }

        printf("  %-12s  %-24s  %8s  %8s  %s\n",
               "width", "throughput (1buf)", "calls",
               "tog (2buf)", "us/call");
        printf("  %-12s  %-24s  %8s  %8s  %s\n",
               "-----------", "-----------------------",
               "--------", "--------", "-------");

        for (di = 0u; di < num_dw; di++) {
            decomp_result_t r1, r2;
            uint16_t w  = disp_w[di];
            size_t   cap = (size_t)w * 10u; /* 10 rows */
            uint16_t *ba, *bb;

            ba = (uint16_t *)malloc(cap * sizeof(uint16_t));
            bb = (uint16_t *)malloc(cap * sizeof(uint16_t));
            if ((ba == NULL) || (bb == NULL)) {
                free(ba); free(bb); continue;
            }

            bench_decompress_cb(comp_data, cs, w,
                                ba, NULL, cap, &r1);
            bench_decompress_cb(comp_data, cs, w,
                                ba, bb, cap, &r2);

            {
                char m1[16], p1[16];
                fmt_tp(r1.timing.avg_s, n, m1, sizeof(m1), p1, sizeof(p1));

                printf("  %5u px      %s (%s)  %6zu    %6zu    %6.1f\n",
                       w, m1, p1,
                       r1.callback_calls,
                       r2.buffer_toggles,
                       r1.timing.avg_s / (double)r1.callback_calls * 1e6);
            }

            free(ba); free(bb);
        }
    }

    /* ======================================================================
     * PART 5 — RLE Stream Structure
     * ====================================================================== */

    print_header("PART 5 — RLE Stream Structure");

    printf("  %-22s  %8s  %8s  %8s  %8s  %8s  %6s\n",
           "pattern", "pixels", "repeat", "literal",
           "total", "comp_B", "ratio");
    printf("  %-22s  %8s  %8s  %8s  %8s  %8s  %6s\n",
           "----------------------", "--------",
           "--------", "--------", "--------", "--------", "------");

    for (pi = 0u; pi < num_pat; pi++) {
        for (si = 0u; si < num_sizes; si++) {
            size_t    n = sizes[si];
            size_t    cs;
            rle_stats_t st;

            patterns[pi](pixels, n);
            cs = rgb565_rle_compress(pixels, n, comp_data, max_cs);
            if (cs == 0u) continue;

            st = analyze_rle(comp_data, cs);

            printf("  %-22s  %8zu  %8zu  %8zu  %8zu  %8zu  %5.1f%%\n",
                   pattern_name(patterns[pi]), n,
                   st.repeat_runs, st.literal_runs,
                   st.total_runs, st.compressed_size,
                   ratio_pct(st.compressed_size, n));
        }
        if (pi + 1u < num_pat) printf("\n");
    }

    /* ======================================================================
     * PART 6 — Simulated DMA: Single-Buf vs Ping-Pong Frame Time
     *
     * Models the key difference:
     *   Single-buffer: callback MUST block (library overwrites buf_a)
     *     → total = CPU_decode + DMA_transfer (serial)
     *   Ping-pong: callback does NOT block (library fills buf_b)
     *     → total ≈ max(CPU_decode, DMA_transfer) (overlapped)
     *
     * Speedup = (CPU + DMA) / max(CPU, DMA)
     *   = 1.0x  when DMA << CPU  (CPU is the bottleneck)
     *   = 2.0x  when DMA ≈ CPU  (balanced — best case)
     *   ≈ 1.0x  when DMA >> CPU  (DMA is the bottleneck)
     *
     * We sweep bus speeds to show all three regimes.
     * ====================================================================== */

    print_header("PART 6 — Ping-Pong Overlap: Measured vs Predicted");

    printf("  Simulates blocking (single-buf) vs non-blocking (ping-pong)\n");
    printf("  callback.  Busy-wait spin models bus throughput.\n");
    printf("\n");

    {
        /*
         * Sweep from "DMA faster than CPU" through "balanced" to
         * "DMA slower than CPU" to show the full speedup curve.
         *
         * 256K px = 512 KB raw.  CPU decode ≈ 1.0 ms (mixed pattern).
         *
         *   Bus        DMA time    Regime
         *   ────────   ────────    ────────────
         *   1024 MB/s   0.5 ms     DMA < CPU     → 1.5x
         *    512 MB/s   1.0 ms     DMA ≈ CPU     → 2.0x (sweet spot)
         *    256 MB/s   2.0 ms     DMA > CPU     → 1.5x
         *    100 MB/s   5.1 ms     DMA >> CPU    → 1.2x
         *     50 MB/s  10.2 ms     DMA >> CPU    → 1.1x
         *     20 MB/s  25.6 ms     DMA >> CPU    → 1.04x
         */
        double bus_speeds[] = {
            1024e6, 512e6, 256e6, 100e6, 50e6, 20e6
        };
        const char *bus_names[] = {
            "1024 MB/s (RAM DMA)",
            "512 MB/s (QSPI/Octal)",
            "256 MB/s (fast 8080)",
            "100 MB/s (8080 16-bit)",
            "50 MB/s (fast SPI)",
            "20 MB/s (typical SPI)"
        };
        size_t   num_bs   = sizeof(bus_speeds) / sizeof(bus_speeds[0]);
        size_t   n        = sizes[3]; /* 256K px */
        size_t   cs;
        uint16_t width    = 320u;
        size_t   buf_px   = (size_t)width * 10u; /* 10 rows */
        uint16_t *ba, *bb;
        size_t   bi;

        fill_mixed(pixels, n);
        cs = rgb565_rle_compress(pixels, n, comp_data, max_cs);
        if (cs == 0u) {
            printf("  compress failed.\n");
            goto done;
        }

        ba = (uint16_t *)malloc(buf_px * sizeof(uint16_t));
        bb = (uint16_t *)malloc(buf_px * sizeof(uint16_t));
        if ((ba == NULL) || (bb == NULL)) {
            free(ba); free(bb);
            printf("  allocation failed.\n");
            goto done;
        }

        printf("  Pattern: mixed, %zu px (%.0f KB raw), width=%u, buf=%zu px (10 rows)\n",
               n, (double)n * 2.0 / 1024.0, width, buf_px);
        printf("\n");
        printf("  %-24s  %10s  %10s  %10s  %10s  %10s  %10s\n",
               "bus speed", "CPU (ms)", "DMA (ms)",
               "1-buf (ms)", "2-buf (ms)",
               "predicted", "measured");
        printf("  %-24s  %10s  %10s  %10s  %10s  %10s  %10s\n",
               "-----------------------", "----------", "----------",
               "----------", "----------", "----------", "----------");

        for (bi = 0u; bi < num_bs; bi++) {
            dma_overlap_t ds1, ds2;
            struct timespec t_start, t_end;
            double  total_1buf, total_2buf;
            double  cpu_s, dma_s, predicted_su, measured_su;
            size_t  dp;

            /* ---- single-buffer: blocking DMA ---- */
            memset(&ds1, 0, sizeof(ds1));
            ds1.bytes_per_sec = bus_speeds[bi];
            ds1.defer_dma     = 0; /* blocking */

            /* warmup */
            rgb565_rle_decompress_callback(comp_data, cs, width,
                                            ba, NULL, buf_px,
                                            cb_dma_overlap, &ds1);

            /* measured */
            memset(&ds1, 0, sizeof(ds1));
            ds1.bytes_per_sec = bus_speeds[bi];
            ds1.defer_dma     = 0;

            clock_gettime(CLOCK_MONOTONIC, &t_start);
            dp = rgb565_rle_decompress_callback(comp_data, cs, width,
                                                 ba, NULL, buf_px,
                                                 cb_dma_overlap, &ds1);
            clock_gettime(CLOCK_MONOTONIC, &t_end);
            total_1buf = time_diff_s(&t_start, &t_end);
            if (dp == 0u) { printf("  decode failed\n"); continue; }

            cpu_s = total_1buf - ds1.dma_time_s;
            dma_s = ds1.dma_time_s;

            /* ---- ping-pong: non-blocking DMA ---- */
            memset(&ds2, 0, sizeof(ds2));
            ds2.bytes_per_sec = bus_speeds[bi];
            ds2.defer_dma     = 1; /* non-blocking */

            /* warmup */
            rgb565_rle_decompress_callback(comp_data, cs, width,
                                            ba, bb, buf_px,
                                            cb_dma_overlap, &ds2);

            /* measured */
            memset(&ds2, 0, sizeof(ds2));
            ds2.bytes_per_sec = bus_speeds[bi];
            ds2.defer_dma     = 1;

            clock_gettime(CLOCK_MONOTONIC, &t_start);
            dp = rgb565_rle_decompress_callback(comp_data, cs, width,
                                                 ba, bb, buf_px,
                                                 cb_dma_overlap, &ds2);
            /* drain remaining DMA: total_DMA - time_already_elapsed */
            {
                struct timespec t_now;
                double remaining;

                clock_gettime(CLOCK_MONOTONIC, &t_now);
                remaining = ds2.pending_dma_s
                          - time_diff_s(&t_start, &t_now);
                if (remaining > 0.0) {
                    spin_until(&t_now, remaining);
                }
            }
            clock_gettime(CLOCK_MONOTONIC, &t_end);
            total_2buf = time_diff_s(&t_start, &t_end);
            if (dp == 0u) { printf("  ping-pong failed\n"); continue; }

            /* analytical prediction */
            predicted_su = (cpu_s + dma_s) /
                           (cpu_s > dma_s ? cpu_s : dma_s);
            measured_su  = total_1buf / total_2buf;

            printf("  %-24s  %10.2f  %10.2f  %10.2f  %10.2f  %9.2fx  %9.2fx\n",
                   bus_names[bi],
                   cpu_s * 1e3, dma_s * 1e3,
                   total_1buf * 1e3,
                   total_2buf * 1e3,
                   predicted_su, measured_su);
        }

        free(ba); free(bb);

        printf("\n");
        printf("  Interpretation:\n");
        printf("    DMA << CPU  → ping-pong hides DMA,     speedup ≈ 1 + DMA/CPU\n");
        printf("    DMA ≈ CPU  → sweet spot,               speedup ≈ 2.0x\n");
        printf("    DMA >> CPU  → CPU hidden by DMA anyway, speedup ≈ 1.0x\n");
        printf("    Most MCU display buses are in the DMA >> CPU regime —\n");
        printf("    ping-pong won't help single-frame decode.  Its real value\n");
        printf("    is multi-frame: decode frame N+1 while DMA sends frame N.\n");
    }

done:
    free(pixels);
    free(comp_data);

    printf("\n");
    printf("  --- notes ---\n");
    printf("  clock_gettime(CLOCK_MONOTONIC) resolution: ~1 ns (nominal)\n");
    printf("  Parts 1-5: %d measured iterations after %d warmups (no-op callback).\n",
           ITERATIONS, WARMUP_ITERS);
    printf("  Part 6: single-shot total frame time with simulated blocking/non-\n");
    printf("          blocking DMA.  Single-buf blocks in callback; ping-pong\n");
    printf("          defers DMA wait → CPU decode overlaps with DMA transfer.\n");
    printf("  Speedup = single_buf_time / ping_pong_time (>1 = ping-pong wins).\n");
    printf("\nDone.\n\n");

    return EXIT_SUCCESS;
}
