# libFLAC - Minimal decoder-only build for ScummVM
# Based on FLAC 1.4.3 - https://github.com/xiph/flac
# License: BSD-3-Clause (see COPYING.Xiph)

add_library(flac_decoder STATIC
    src/stream_decoder.c
    src/bitreader.c
    src/bitmath.c
    src/cpu.c
    src/crc.c
    src/fixed.c
    src/format.c
    src/lpc.c
    src/md5.c
    src/memory.c
    src/metadata_iterators.c
    src/metadata_object.c
)

target_include_directories(flac_decoder PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(flac_decoder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include/private
    ${CMAKE_CURRENT_SOURCE_DIR}/include/protected
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(flac_decoder PRIVATE
    HAVE_CONFIG_H=1
    FLAC__NO_DLL=1
)

# Suppress warnings in third-party code
target_compile_options(flac_decoder PRIVATE
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-implicit-fallthrough
)

set_target_properties(flac_decoder PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 99
)
