# ---------------------------------------------------------------------------
# Download stb_image.h (single-header image loader, used by img2rle)
# ---------------------------------------------------------------------------
set(STB_IMAGE_URL
    "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
)
set(STB_IMAGE_DEST "${CMAKE_CURRENT_BINARY_DIR}/stb_image.h")

if(NOT EXISTS "${STB_IMAGE_DEST}")
    message(STATUS "Downloading stb_image.h …")
    file(DOWNLOAD "${STB_IMAGE_URL}" "${STB_IMAGE_DEST}"
         STATUS STB_DL_STATUS
         TLS_VERIFY ON)

    list(GET STB_DL_STATUS 0 STB_DL_CODE)
    if(NOT STB_DL_CODE EQUAL 0)
        list(GET STB_DL_STATUS 1 STB_DL_MSG)
        message(FATAL_ERROR
            "Failed to download stb_image.h: ${STB_DL_MSG}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Download stb_image_write.h (single-header image writer, used by rle2img)
# ---------------------------------------------------------------------------
set(STB_IMAGE_WRITE_URL
    "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"
)
set(STB_IMAGE_WRITE_DEST "${CMAKE_CURRENT_BINARY_DIR}/stb_image_write.h")

if(NOT EXISTS "${STB_IMAGE_WRITE_DEST}")
    message(STATUS "Downloading stb_image_write.h …")
    file(DOWNLOAD "${STB_IMAGE_WRITE_URL}" "${STB_IMAGE_WRITE_DEST}"
         STATUS STB_DL_STATUS
         TLS_VERIFY ON)

    list(GET STB_DL_STATUS 0 STB_DL_CODE)
    if(NOT STB_DL_CODE EQUAL 0)
        list(GET STB_DL_STATUS 1 STB_DL_MSG)
        message(FATAL_ERROR
            "Failed to download stb_image_write.h: ${STB_DL_MSG}")
    endif()
endif()

# ---------------------------------------------------------------------------
# rle_utils — shared helpers
# ---------------------------------------------------------------------------
add_library(rle_utils STATIC rle_utils.c)
target_include_directories(rle_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rle_utils PRIVATE rgb565_rle)
target_compile_features(rle_utils PRIVATE c_std_99)

# ---------------------------------------------------------------------------
# img2rle
# ---------------------------------------------------------------------------
add_executable(img2rle img2rle.c)
target_include_directories(img2rle PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(img2rle PRIVATE rle_utils rgb565_rle)
target_compile_features(img2rle PRIVATE c_std_99)

# ---------------------------------------------------------------------------
# video2rle
# ---------------------------------------------------------------------------
add_executable(video2rle video2rle.c)
target_include_directories(video2rle PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(video2rle PRIVATE rle_utils rgb565_rle)
target_compile_features(video2rle PRIVATE c_std_99)

# ---------------------------------------------------------------------------
# rle2img — RLE back to image (PNG / JPEG / BMP / TGA)
# ---------------------------------------------------------------------------
add_executable(rle2img rle2img.c)
target_include_directories(rle2img PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(rle2img PRIVATE rgb565_rle)
target_compile_features(rle2img PRIVATE c_std_99)
