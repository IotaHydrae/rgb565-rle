# RGB565 RLE

A platform-independent C library for compressing and decompressing RGB565 pixel data using Run-Length Encoding (RLE).

## Features

- **Zero dependencies** — only `<stddef.h>` and `<stdint.h>`, no libc calls
- **Freestanding ready** — works on bare-metal MCUs, MPUs, Linux userspace, and kernel space
- **Endianness-portable** — compressed format is identical regardless of platform byte order
- **Simple API** — three functions, all returning 0 on error

## Compression Format

```
Header: uint32_t pixel_count (little-endian)
Body:   repeated [control_byte][pixel_data...]
```

| Control byte | Meaning |
|---|---|
| `0xxxxxxx` | Literal run: `(xxxxxxx + 1)` distinct 16-bit pixels follow |
| `1xxxxxxx` | Repeat run: one 16-bit pixel follows, repeated `(xxxxxxx + 1)` times |

Maximum 128 pixels per run. All multi-byte values are little-endian.

## API

```c
// Worst-case compressed buffer size (4 + pixel_count * 3)
size_t rgb565_rle_max_compressed_size(size_t pixel_count);

// Compress: returns bytes written, or 0 on error
size_t rgb565_rle_compress(const uint16_t *pixels, size_t pixel_count,
                           uint8_t *output, size_t output_capacity);

// Decompress: returns pixel count written, or 0 on error
size_t rgb565_rle_decompress(const uint8_t *input, size_t input_size,
                             uint16_t *pixels, size_t pixel_capacity);
```

## Building

```bash
mkdir build && cd build
cmake .. -DRGB565_RLE_BUILD_TESTS=ON -DRGB565_RLE_BUILD_EXAMPLES=ON
cmake --build .
ctest --output-on-failure
```

### CMake Options

| Option | Default | Description |
|---|---|---|
| `RGB565_RLE_BUILD_EXAMPLES` | ON | Build example programs |
| `RGB565_RLE_BUILD_TESTS` | ON | Build test suite |
| `RGB565_RLE_BUILD_TOOLS` | ON | Build conversion tools (`img2rle`, `video2rle`) |

### Integrating into Your Project

```cmake
add_subdirectory(path/to/rgb565-rle)
target_link_libraries(your_target PRIVATE rgb565_rle)
```

Or install system-wide:

```bash
cmake --install build --prefix /usr/local
```

## Example

```c
#include "rgb565_rle.h"

uint16_t pixels[] = { 0xF800, 0xF800, 0xF800, 0x07E0, 0x001F };

// Allocate worst-case output buffer
size_t max_size = rgb565_rle_max_compressed_size(5);
uint8_t *compressed = malloc(max_size);

// Compress
size_t comp_size = rgb565_rle_compress(pixels, 5, compressed, max_size);

// Decompress
uint16_t restored[5];
size_t count = rgb565_rle_decompress(compressed, comp_size, restored, 5);
```

## Tools

The project includes conversion tools that turn images and videos into RLE-compressed C headers or binary files.

### img2rle — Image to RLE

Converts PNG, JPEG, BMP, and other common image formats to RGB565 RLE.

```bash
# C header output (default)
./tools/img2rle input.png -o image.h

# Binary output
./tools/img2rle input.png -t bin -o image.bin

# Resize during conversion
./tools/img2rle input.png -w 64 -h 64 -n my_sprite -o sprite.h
```

### video2rle — Video / Frame Sequence to RLE

Converts image sequences or raw RGB565 frames into per-frame RLE-compressed output.

```bash
# Image sequence mode
./tools/video2rle frame_0001.png frame_0002.png frame_0003.png -o video.h

# Binary container output
./tools/video2rle frame_*.png -t bin -o video.bin

# Raw RGB565 frames
./tools/video2rle --raw frames.bin -w 320 -h 240 -o video.h
```

## License

MIT
