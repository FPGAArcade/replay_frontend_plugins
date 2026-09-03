# VICE, built as the C64 plugin. The emulator is the libretro port of VICE, carried in-tree under
# upstream/ and built by its own CMake into a static library the wrapper links.

# ReSID is C++, so the emulator needs more than the C the top-level project enables by default.
enable_language(CXX)

add_subdirectory(upstream "${CMAKE_CURRENT_BINARY_DIR}/vice_build" EXCLUDE_FROM_ALL)

add_replay_emu_plugin(NAME vice_c64 SOURCES vice_c64_core.c)
target_link_libraries(vice_c64 PRIVATE vice_c64_lib)
target_include_directories(vice_c64 PRIVATE
    upstream
    upstream/libretro
    upstream/libretro-common/include
    upstream/vice/src)
target_compile_options(vice_c64 PRIVATE
    -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers)

# VICE and libretro-common define names the host also exports -- log_message is the one that bit --
# and without this the dynamic loader would bind VICE's internal calls to the host's versions.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(vice_c64 PRIVATE -Wl,-Bsymbolic)
endif()
