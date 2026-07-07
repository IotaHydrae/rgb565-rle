cmake_minimum_required(VERSION 3.10)
project(rgb565-rle VERSION 1.0.0 LANGUAGES C)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(RGB565_RLE_BUILD_EXAMPLES "Build example programs"      ON)
option(RGB565_RLE_BUILD_TESTS    "Build test suite"           ON)
option(RGB565_RLE_BUILD_TOOLS    "Build conversion tools"     ON)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(rgb565_rle STATIC
    src/rgb565_rle.c
)

target_include_directories(rgb565_rle
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(rgb565_rle PUBLIC c_std_99)

# The library is deliberately freestanding-friendly.
# No -ffreestanding here, so the build host can link it with hosted
# programs (examples / tests).  Users targeting bare-metal should
# compile with -ffreestanding themselves.

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS rgb565_rle
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# ---------------------------------------------------------------------------
# Sub-directories
# ---------------------------------------------------------------------------
if(RGB565_RLE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(RGB565_RLE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(RGB565_RLE_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
