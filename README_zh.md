# RGB565 RLE

一个平台无关的 C 语言库，使用游程编码（RLE）对 RGB565 像素数据进行压缩和解压。

[English](README.md)

## 特性

- **零依赖** — 仅使用 `<stddef.h>` 和 `<stdint.h>`，不调用任何 libc 函数
- **独立环境可用** — 可在裸机 MCU、MPU、Linux 用户空间及内核空间运行
- **端序无关** — 压缩格式不受平台字节序影响，跨平台一致
- **简洁 API** — 仅三个函数，出错时均返回 0

## 压缩格式

```
头部: uint32_t pixel_count (小端序)
数据体: 重复的 [控制字节][像素数据...]
```

| 控制字节 | 含义 |
|---|---|
| `0xxxxxxx` | 字面量游程: 后跟 `(xxxxxxx + 1)` 个不同的 16 位像素 |
| `1xxxxxxx` | 重复游程: 后跟 1 个 16 位像素, 重复 `(xxxxxxx + 1)` 次 |

每个游程最多 128 像素。所有多字节值均使用小端序存储。

## API

```c
// 计算最坏情况下压缩缓冲区大小 (4 + pixel_count * 3)
size_t rgb565_rle_max_compressed_size(size_t pixel_count);

// 压缩: 返回写入的字节数, 出错返回 0
size_t rgb565_rle_compress(const uint16_t *pixels, size_t pixel_count,
                           uint8_t *output, size_t output_capacity);

// 解压: 返回写入的像素数, 出错返回 0
size_t rgb565_rle_decompress(const uint8_t *input, size_t input_size,
                             uint16_t *pixels, size_t pixel_capacity);
```

## 构建

```bash
mkdir build && cd build
cmake .. -DRGB565_RLE_BUILD_TESTS=ON -DRGB565_RLE_BUILD_EXAMPLES=ON
cmake --build .
ctest --output-on-failure
```

### CMake 选项

| 选项 | 默认值 | 说明 |
|---|---|---|
| `RGB565_RLE_BUILD_EXAMPLES` | ON | 构建示例程序 |
| `RGB565_RLE_BUILD_TESTS` | ON | 构建测试套件 |

### 集成到你的项目

```cmake
add_subdirectory(path/to/rgb565-rle)
target_link_libraries(your_target PRIVATE rgb565_rle)
```

或者安装到系统目录:

```bash
cmake --install build --prefix /usr/local
```

## 示例

```c
#include "rgb565_rle.h"

uint16_t pixels[] = { 0xF800, 0xF800, 0xF800, 0x07E0, 0x001F };

// 分配最坏情况输出缓冲区
size_t max_size = rgb565_rle_max_compressed_size(5);
uint8_t *compressed = malloc(max_size);

// 压缩
size_t comp_size = rgb565_rle_compress(pixels, 5, compressed, max_size);

// 解压
uint16_t restored[5];
size_t count = rgb565_rle_decompress(compressed, comp_size, restored, 5);
```

## 许可证

MIT
