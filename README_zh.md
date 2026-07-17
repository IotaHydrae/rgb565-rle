# RGB565 RLE

一个平台无关、零依赖的 C 语言库，使用游程编码（RLE）对 RGB565 像素数据进行压缩和解压。

[English](README.md)

## 快速开始

```c
#include "rgb565_rle.h"

// === 压缩 ===
uint16_t pixels[] = { 0xF800, 0xF800, 0xF800, 0x07E0, 0x001F };

size_t  max_size = rgb565_rle_max_compressed_size(5);
uint8_t compressed[max_size];

size_t comp_size = rgb565_rle_compress(pixels, 5, compressed, max_size);

// === 解压（完整帧缓冲） ===
uint16_t restored[5];
size_t   count = rgb565_rle_decompress(compressed, comp_size, restored, 5);
```

### 流式解压（适用于 MCU）

当内存不足以分配完整帧缓冲时，可以边解码边批量刷新：

```c
#include "rgb565_rle.h"

static void lcd_flush(const uint16_t *pixels, size_t count,
                      uint16_t xs, uint16_t ys,
                      uint16_t xe, uint16_t ye,
                      void *user_data)
{
    lcd_set_window(xs, ys, xe, ye);   // 设置显示窗口 CASET / RASET
    lcd_write_pixels(pixels, count);  // DMA 或 8080/SPI 推屏
}

// 单缓冲模式：一行 = 宽度像素 = 640 字节
uint16_t line_buf[320];
rgb565_rle_decompress_callback(compressed, comp_size,
                               320,              // 图像宽度
                               line_buf, NULL,   // buf_a, buf_b = NULL → 单缓冲
                               320,              // buf_capacity（像素数）
                               lcd_flush, NULL);

// 乒乓缓冲模式：DMA 发送 buf_a 的同时，CPU 解码到 buf_b
uint16_t buf_a[320], buf_b[320];
rgb565_rle_decompress_callback(compressed, comp_size,
                               320,              // 图像宽度
                               buf_a, buf_b,     // buf_b ≠ NULL → 乒乓模式
                               320,              // buf_capacity（像素数）
                               lcd_flush, NULL);
```

**为什么重要：** 320×480 的屏幕，完整帧缓冲需要 300 KB RAM。使用回调 API 后，你只需分配任意大小的 buffer — 10 行、1 行、50 像素 — 由你的 MCU 剩余内存决定。

### CMake 集成

```cmake
add_subdirectory(path/to/rgb565-rle)
target_link_libraries(your_target PRIVATE rgb565_rle)
```

或者直接将 `include/rgb565_rle.h` 和 `src/rgb565_rle.c` 复制到你的项目中 — 零依赖。

## 特性

- **零依赖** — 仅使用 `<stddef.h>` 和 `<stdint.h>`，不调用任何 libc 函数，无动态内存分配
- **独立环境可用** — 裸机 MCU、RTOS、Linux 用户空间、内核模块，同一套代码
- **端序无关** — 所有多字节值通过显式字节位移读写，不依赖平台字节序
- **流式解码** — 基于回调的 API：自行提供缓冲区，在 RAM 占用与回调频率之间灵活权衡
- **乒乓双缓冲** — CPU 解码与 DMA 刷新重叠进行，充分利用硬件并行
- **46 个单元测试** — 往返正确性、边界条件（128 像素游程）、损坏数据、64K 压力、多行合并

## 压缩格式

```
头部: uint32_t pixel_count（小端序）
体:   重复的 [控制字节][像素数据...]
```

| 控制字节 | 名称 | 含义 |
|---|---|---|
| `0nnn nnnn` | 字面量游程 | 后跟 `(nnnnnnn + 1)` 个不同的 RGB565 像素 |
| `1nnn nnnn` | 重复游程 | 后跟 1 个 RGB565 像素，重复 `(nnnnnnn + 1)` 次 |

每个游程最多 128 像素。最坏情况压缩大小：`4 + 像素数 × 3` 字节。

## API 参考

### `rgb565_rle_max_compressed_size`

```c
size_t rgb565_rle_max_compressed_size(size_t pixel_count);
```

返回最坏情况输出缓冲区大小：`4 + pixel_count × 3`。`pixel_count` 为 0 或溢出时返回 0。

### `rgb565_rle_compress`

```c
size_t rgb565_rle_compress(const uint16_t *pixels,
                           size_t pixel_count,
                           uint8_t *output,
                           size_t output_capacity);
```

返回写入的字节数，出错返回 0（空指针、零长度、缓冲区不足）。

### `rgb565_rle_decompress`

```c
size_t rgb565_rle_decompress(const uint8_t *input,
                             size_t input_size,
                             uint16_t *pixels,
                             size_t pixel_capacity);
```

返回写入的像素数，出错返回 0（空指针、截断/损坏数据、缓冲区不足）。

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

面向内存受限系统的流式解码。将像素填入用户提供的缓冲区，缓冲区满或全部像素解码完成时回调。

| 参数 | 说明 |
|---|---|
| `width` | 图像宽度（像素），必须 > 0 |
| `buf_a` | 累积缓冲区（不可为 NULL） |
| `buf_b` | 乒乓模式的第二缓冲区，NULL 表示单缓冲模式 |
| `buf_capacity` | **每个**缓冲区的容量（像素数），必须 > 0 |
| `callback` | 每批次调用一次；坐标 `(xs,ys)→(xe,ye)` 为包围矩形 |
| `user_data` | 透传给回调的不透明指针 |

回调坐标让你可以在推送像素前设置显示窗口 — 非常适合 SPI/8080 并口 LCD 驱动。

**缓冲区模式：**
- `buf_b == NULL` → 单缓冲：解码到 `buf_a` → 回调 → 复用
- `buf_b != NULL` → 乒乓缓冲：回调消费 `buf_a` 的同时，库填充 `buf_b`，交替切换

**缓冲区容量权衡：**

| `buf_capacity` | 回调频率 | RAM（16 位字） |
|---|---|---|
| 320 px（QVGA 1 行） | 240 次/帧 | 640 字节 |
| 3200 px（10 行） | 24 次/帧 | 6.25 KB |
| 153600 px（完整 QVGA） | 1 次/帧 | 300 KB |

## 构建

```bash
mkdir build && cd build
cmake .. -DRGB565_RLE_BUILD_TESTS=ON -DRGB565_RLE_BUILD_EXAMPLES=ON -DRGB565_RLE_BUILD_TOOLS=ON
cmake --build .
ctest --output-on-failure
```

### CMake 选项

| 选项 | 默认值 | 说明 |
|---|---|---|
| `RGB565_RLE_BUILD_EXAMPLES` | ON | 示例程序（`encode_example`、`decode_example`） |
| `RGB565_RLE_BUILD_TESTS` | ON | 测试套件（3 个目标，46 个测试） |
| `RGB565_RLE_BUILD_TOOLS` | ON | 转换工具（`img2rle`、`video2rle`） |

## 工具

将图片和视频转换为 RLE 压缩的 C 头文件或二进制文件。

### img2rle — 图片转 RLE

```bash
./tools/img2rle input.png -o image.h          # C 头文件（默认）
./tools/img2rle input.png -t bin -o image.bin # 二进制 RLE 原始流
./tools/img2rle input.png -w 64 -h 64 -n sprite -o sprite.h  # 缩放 + 命名
```

支持 PNG、JPEG、BMP 等 stb_image 能读取的所有格式。

### video2rle — 帧序列转 RLE

图片序列或原始 RGB565 帧 → 逐帧 RLE 压缩输出，含偏移表。

```bash
./tools/video2rle frame_*.png -o video.h           # 图片序列 → C 头文件
./tools/video2rle frame_*.png -t bin -o video.bin  # 二进制容器
./tools/video2rle --raw frames.bin -w 320 -h 240 -o video.h  # 原始 RGB565 帧
```

## 性能

真实图像数据的典型压缩比：

| 内容类型 | 压缩比 | 示例 |
|---|---|---|
| 纯色 / UI 元素 | 10:1 – 100:1 | 工具栏、背景 |
| 渐变 / 平滑区域 | 3:1 – 10:1 | 天空、阴影 |
| 照片 / 噪声 | 1:1 – 2:1 | — |

最坏情况（交替像素）：零压缩，比原始数据大 50%。

## 许可证

MIT
