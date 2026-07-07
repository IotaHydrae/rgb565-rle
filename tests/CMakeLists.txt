# ---------------------------------------------------------------------------
# test_rgb565_rle
# ---------------------------------------------------------------------------
add_executable(test_rgb565_rle test_rgb565_rle.c)
target_link_libraries(test_rgb565_rle PRIVATE rgb565_rle)
target_compile_features(test_rgb565_rle PRIVATE c_std_99)

add_test(NAME rgb565_rle COMMAND test_rgb565_rle)
