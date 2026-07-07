# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test

```bash
# Configure (from repo root)
mkdir build && cd build
cmake .. -DRGB565_RLE_BUILD_TESTS=ON -DRGB565_RLE_BUILD_EXAMPLES=ON -DRGB565_RLE_BUILD_TOOLS=ON

# Build
cmake --build .

# Run all tests (3 registered: rgb565_rle, compression_ratio, benchmark)
ctest --output-on-failure

# Run a single test binary with verbose output
./tests/test_rgb565_rle
./tests/test_compression_ratio
./tests/test_benchmark
```

CMake options:

| Option | Default | Description |
|---|---|---|
| `RGB565_RLE_BUILD_EXAMPLES` | ON | Example programs |
| `RGB565_RLE_BUILD_TESTS` | ON | Test suite (3 targets) |
| `RGB565_RLE_BUILD_TOOLS` | ON | Conversion tools (`img2rle`, `video2rle`) |

## Project Structure

```
├── include/rgb565_rle.h    — Public API (freestanding)
├── src/rgb565_rle.c        — Library implementation (zero deps)
├── examples/               — encode_example, decode_example
├── tests/                  — test_rgb565_rle, test_compression_ratio, test_benchmark
├── tools/                  — img2rle, video2rle, rle_utils
│   └── (stb_image.h auto-downloaded to build dir by CMake)
└── CLAUDE.md
```

## Architecture

This is a **platform-independent RLE compression library for RGB565 pixel data**. The library itself (`src/rgb565_rle.c`) depends on **nothing but `<stddef.h>` and `<stdint.h>`** — no dynamic allocation, no libc calls, no platform APIs. It is designed to compile in freestanding environments: bare-metal MCUs, MPUs, Linux kernel, etc. The examples, tests, and tools are regular hosted C programs that link the same library.

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

The encoder never emits a repeat run of length 1 — it costs the same as a literal (3 bytes) but would fragment a potential longer literal run.

### Video Binary Container Format

`video2rle -t bin` produces a simple container:
```
[uint32_t frame_count (LE)]
[uint32_t offsets[frame_count + 1] (LE)]   // offset[N+1] = total file size
[frame 0 RLE stream]
[frame 1 RLE stream]
...
```
Each frame's RLE stream is the standard 4-byte-pixel_count-header + body format.

## Tools

### img2rle — Image to RLE

Converts PNG/JPEG/BMP/etc. to RGB565 RLE via stb_image. Outputs `.h` (C header with array + dimension defines) or `.bin` (raw RLE stream). Supports nearest-neighbour resize (`-w`/`-h`). Prints size comparison with savings percentage on completion.

### video2rle — Frame Sequence to RLE

Two input modes:
- **Image sequence**: `video2rle frame_*.png -o video.h`
- **Raw RGB565**: `video2rle --raw frames.bin -w W -h H`

Output `.h` includes per-frame offset table and concatenated frame data. Output `.bin` uses the container format described above.

Both tools share `rle_utils` (`tools/rle_utils.c`) for writing C headers and printing size comparisons. stb_image is downloaded by CMake at configure time; HDR and linear support are disabled (`STBI_NO_HDR`, `STBI_NO_LINEAR`) to avoid a libm dependency.

## Testing

Three test executables registered with CTest:

| Test | Coverage |
|---|---|
| `test_rgb565_rle` | 25 unit tests: round-trip, edge cases (128-px runs), parameter validation, corrupted data, 64K stress |
| `test_compression_ratio` | 10 patterns at 1024 px + sweeps across sizes 64–16384 px for solid and noise; worst-case bound table |
| `test_benchmark` | 4 patterns × 5 sizes (up to 1M px), 5 iterations each; reports MB/s and MPixels/s for compress and decompress |

All tests use inline harnesses — no external test framework dependency.
