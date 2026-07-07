# ---------------------------------------------------------------------------
# test_rgb565_rle
# ---------------------------------------------------------------------------
add_executable(test_rgb565_rle test_rgb565_rle.c)
target_link_libraries(test_rgb565_rle PRIVATE rgb565_rle)
target_compile_features(test_rgb565_rle PRIVATE c_std_99)

add_test(NAME rgb565_rle COMMAND test_rgb565_rle)

# ---------------------------------------------------------------------------
# test_compression_ratio
# ---------------------------------------------------------------------------
add_executable(test_compression_ratio test_compression_ratio.c)
target_link_libraries(test_compression_ratio PRIVATE rgb565_rle)
target_compile_features(test_compression_ratio PRIVATE c_std_99)

add_test(NAME compression_ratio COMMAND test_compression_ratio)

# ---------------------------------------------------------------------------
# test_benchmark
# ---------------------------------------------------------------------------
add_executable(test_benchmark test_benchmark.c)
target_link_libraries(test_benchmark PRIVATE rgb565_rle)
target_compile_features(test_benchmark PRIVATE c_std_99)

add_test(NAME benchmark COMMAND test_benchmark)
