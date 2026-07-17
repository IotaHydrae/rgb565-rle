# RGB565 RLE

A platform-independent, zero-dependency C library for compressing and decompressing
RGB565 pixel data using Run-Length Encoding (RLE).

[中文](README_zh.md)

## Quick Start

```c
#include "rgb565_rle.h"

// === Compress ===
uint16_t pixels[] = { 0xF800, 0xF800, 0xF800, 0x07E0, 0x001F };

size_t  max_size = rgb565_rle_max_compressed_size(5);
uint8_t compressed[max_size];

size_t comp_size = rgb565_rle_compress(pixels, 5, compressed, max_size);

// === Decompress (full buffer) ===
uint16_t restored[5];
size_t   count = rgb565_rle_decompress(compressed, comp_size, restored, 5);
```

### Streaming Decompress (for MCUs)

When you can't afford a full frame buffer, decode pixel-by-pixel and flush in batches:

```c
#include "rgb565_rle.h"

static void lcd_flush(const uint16_t *pixels, size_t count,
                      uint16_t xs, uint16_t ys,
                      uint16_t xe, uint16_t ye,
                      void *user_data)
{
    lcd_set_window(xs, ys, xe, ye);   // CASET / RASET
    lcd_write_pixels(pixels, count);  // DMA or 8080/SPI push
}

// Single-buffer: one line = width pixels = 640 bytes
uint16_t line_buf[320];
rgb565_rle_decompress_callback(compressed, comp_size,
                               320,              // image width
                               line_buf, NULL,   // buf_a, buf_b = NULL → single
                               320,              // buf_capacity (pixels)
                               lcd_flush, NULL);

// Ping-pong: decode into buf_b while DMA sends buf_a
uint16_t buf_a[320], buf_b[320];
rgb565_rle_decompress_callback(compressed, comp_size,
                               320,              // image width
                               buf_a, buf_b,     // buf_b ≠ NULL → ping-pong
                               320,              // buf_capacity (pixels)
                               lcd_flush, NULL);
```

**Why this matters:** With a full frame buffer, a 320×480 display needs 300 KB of RAM. With the callback API, you control the buffer size — 10 rows, 1 row, 50 pixels — whatever your MCU can spare.

### CMake Integration

```cmake
add_subdirectory(path/to/rgb565-rle)
target_link_libraries(your_target PRIVATE rgb565_rle)
```

Or copy `include/rgb565_rle.h` and `src/rgb565_rle.c` directly into your project — they have zero dependencies.

## Features

- **Zero dependencies** — only `<stddef.h>` and `<stdint.h>`, no libc calls, no dynamic allocation
- **Freestanding ready** — bare-metal MCU, RTOS, Linux userspace, kernel module — same code
- **Endianness-portable** — all multi-byte values stored as explicit LE bytes, never by pointer cast
- **Streaming decode** — callback-based API: provide your own buffer, control RAM vs callback frequency
- **Ping-pong double buffering** — overlap CPU decode with DMA display transfer
- **46 unit tests** — round-trip, edge cases (128-px runs), corrupted data, 64K stress, multi-row batching

## Compression Format

```
Header: uint32_t pixel_count (little-endian)
Body:   repeated [control_byte][pixel_data...]
```

| Control byte | Name | Meaning |
|---|---|---|
| `0nnn nnnn` | Literal run | `(nnnnnnn + 1)` distinct RGB565 pixels follow |
| `1nnn nnnn` | Repeat run | one RGB565 pixel follows, repeated `(nnnnnnn + 1)` times |

Maximum 128 pixels per run. Worst-case compressed size: `4 + pixel_count × 3` bytes.

## API Reference

### `rgb565_rle_max_compressed_size`

```c
size_t rgb565_rle_max_compressed_size(size_t pixel_count);
```

Returns the worst-case output buffer size: `4 + pixel_count × 3`. Returns 0 if `pixel_count` is 0 or would overflow.

### `rgb565_rle_compress`

```c
size_t rgb565_rle_compress(const uint16_t *pixels,
                           size_t pixel_count,
                           uint8_t *output,
                           size_t output_capacity);
```

Returns bytes written, or 0 on error (NULL pointer, zero count, insufficient capacity).

### `rgb565_rle_decompress`

```c
size_t rgb565_rle_decompress(const uint8_t *input,
                             size_t input_size,
                             uint16_t *pixels,
                             size_t pixel_capacity);
```

Returns pixels written, or 0 on error (NULL pointer, truncated/corrupted data, insufficient capacity).

### `rgb565_rle_decompress_callback`

```c
typedef void (*rgb565_rle_callback)(const uint16_t *pixels, size_t count,
                                    uint16_t xs, uint16_t ys,
                                    uint16_t xe, uint16_t ye,
                                    void *user_data);

size_t rgb565_rle_decompress_callback(const uint8_t *input,
                                      size_t input_size,
                                      uint16_t width,
                                      uint16_t *buf_a,
                                      uint16_t *buf_b,
                                      size_t buf_capacity,
                                      rgb565_rle_callback callback,
                                      void *user_data);
```

Streaming decode for memory-constrained systems. Fills the caller-provided buffer and invokes `callback` when the buffer is full or all pixels are decoded.

| Parameter | Description |
|---|---|
| `width` | Image width in pixels (> 0) |
| `buf_a` | Accumulation buffer (must not be NULL) |
| `buf_b` | Second buffer for ping-pong, or NULL for single-buffer mode |
| `buf_capacity` | Capacity of **each** buffer in pixels (> 0) |
| `callback` | Called for each batch; coordinates `(xs,ys)→(xe,ye)` are the bounding rectangle |
| `user_data` | Opaque pointer passed through to callback |

The callback coordinates let you set a display window before pushing pixels — ideal for SPI/8080-parallel LCD drivers.

**Buffer mode:**
- `buf_b == NULL` → single buffer: decode into `buf_a`, callback, reuse
- `buf_b != NULL` → ping-pong: while callback consumes `buf_a`, decode into `buf_b`, toggle

**Buffer capacity trade-off:**

| `buf_capacity` | Callback frequency | RAM (16-bit words) |
|---|---|---|
| 320 px (1 row of QVGA) | 240 calls/frame | 640 bytes |
| 3200 px (10 rows) | 24 calls/frame | 6.25 KB |
| 153600 px (full QVGA) | 1 call/frame | 300 KB |

## Building

```bash
mkdir build && cd build
cmake .. -DRGB565_RLE_BUILD_TESTS=ON -DRGB565_RLE_BUILD_EXAMPLES=ON -DRGB565_RLE_BUILD_TOOLS=ON
cmake --build .
ctest --output-on-failure
```

### CMake Options

| Option | Default | Description |
|---|---|---|
| `RGB565_RLE_BUILD_EXAMPLES` | ON | Example programs (`encode_example`, `decode_example`) |
| `RGB565_RLE_BUILD_TESTS` | ON | Test suite (3 targets, 46 tests) |
| `RGB565_RLE_BUILD_TOOLS` | ON | Conversion tools (`img2rle`, `video2rle`) |

## Tools

Conversion tools that turn images and videos into RLE-compressed C headers or binary files.

### img2rle — Image to RLE

```bash
./tools/img2rle input.png -o image.h          # C header (default)
./tools/img2rle input.png -t bin -o image.bin # binary raw RLE stream
./tools/img2rle input.png -w 64 -h 64 -n sprite -o sprite.h  # resize + name
```

Supports PNG, JPEG, BMP, and any format stb_image can read.

### video2rle — Frame Sequence to RLE

Image sequence or raw RGB565 frames → per-frame RLE-compressed output with offset table.

```bash
./tools/video2rle frame_*.png -o video.h           # image sequence → C header
./tools/video2rle frame_*.png -t bin -o video.bin  # binary container
./tools/video2rle --raw frames.bin -w 320 -h 240 -o video.h  # raw RGB565 frames
```

## Performance

Typical compression ratios on real image data:

| Content type | Ratio | Example |
|---|---|---|
| Solid color / UI elements | 10:1 – 100:1 | Toolbar, background |
| Gradient / smooth areas | 3:1 – 10:1 | Sky, shadows |
| Photographic / noisy | 1:1 – 2:1 | — |

Worst case (alternating pixels): zero compression, 50% expansion over raw.

## License

MIT
