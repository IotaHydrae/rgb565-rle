# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test

```bash
# Configure (from repo root)
mkdir build && cd build
cmake .. -DRGB565_RLE_BUILD_TESTS=ON -DRGB565_RLE_BUILD_EXAMPLES=ON

# Build
cmake --build .

# Run all tests
ctest --output-on-failure

# Run a single test binary with verbose output
./tests/test_rgb565_rle
```

CMake options: `RGB565_RLE_BUILD_EXAMPLES` (default ON), `RGB565_RLE_BUILD_TESTS` (default ON).

## Architecture

This is a **platform-independent RLE compression library for RGB565 pixel data**. The library itself (`src/rgb565_rle.c`) depends on **nothing but `<stddef.h>` and `<stdint.h>`** — no dynamic allocation, no libc calls, no platform APIs. It is designed to compile in freestanding environments: bare-metal MCUs, MPUs, Linux kernel, etc. The examples and tests are regular hosted C programs that link the same library.

### Wire Format

Every compressed stream has a 4-byte little-endian header (uint32_t `pixel_count`) followed by repeating control+data blocks:

| Bit 7 of control byte | Bits 6–0 | Meaning |
|---|---|---|
| 0 | `count − 1` | Literal run: `count` distinct pixels follow (2 bytes each, LE) |
| 1 | `count − 1` | Repeat run: one pixel follows (2 bytes, LE), repeated `count` times |

Max run length is 128 pixels (`RGB565_RLE_MAX_RUN_LENGTH`). All multi-byte values are little-endian — the library reads/writes bytes explicitly with shift+mask, never by casting pointers, so the compressed format is identical regardless of platform endianness.

Worst-case compressed size: `4 + pixel_count × 3` bytes (every pixel is a 1-pixel literal run). Use `rgb565_rle_max_compressed_size()` to size the output buffer.

### API (`include/rgb565_rle.h`)

All three functions return 0 on error (NULL pointers, zero counts, insufficient buffers, corrupted/truncated data, integer overflow).

- `rgb565_rle_max_compressed_size(pixel_count)` — worst-case output buffer size
- `rgb565_rle_compress(pixels, count, output, capacity)` — returns bytes written
- `rgb565_rle_decompress(input, input_size, pixels, capacity)` — returns pixel count written

### Compression Algorithm

Greedy single-pass encoder in `rgb565_rle_compress()`:
1. At each position, scan forward for a repeat run (≥2 consecutive identical pixels). Cap at 128.
2. If found: emit a repeat control byte + the pixel value.
3. If not found: gather a literal run of non-repeating pixels, stopping when a repeat of ≥2 is detected. Emit a literal control byte + the distinct pixels.

The encoder never emits a repeat run of length 1 because it costs the same as a literal (3 bytes) but would fragment a potential longer literal run.

### Testing

The test suite (`tests/test_rgb565_rle.c`) uses an inline harness (no external framework). Tests cover round-trip for various patterns, edge cases around the 128-pixel run limit, parameter validation (NULL, zero, insufficient buffers), corrupted/truncated data, and a 64K-pixel stress test.
