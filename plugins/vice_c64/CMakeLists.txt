# VICE, built as the C64 plugin. The emulator is the libretro port of VICE, carried in-tree under
# upstream/ and built by its own CMake into a static library the wrapper links.

# ReSID is C++, so the emulator needs more than the C the top-level project enables by default.
enable_language(CXX)

add_subdirectory(upstream "${CMAKE_CURRENT_BINARY_DIR}/vice_build" EXCLUDE_FROM_ALL)
# VICE builds with -w, which silences -Werror=date-time, and its banner prints __DATE__ __TIME__.
# A fixed SOURCE_DATE_EPOCH keeps the plugin byte-identical across rebuilds.
set_target_properties(vice_c64_lib PROPERTIES
    C_COMPILER_LAUNCHER "${CMAKE_COMMAND};-E;env;SOURCE_DATE_EPOCH=0")

add_replay_emu_plugin(NAME vice_c64 SOURCES vice_c64_core.c)
# dladdr is in libdl, not libc, on the glibc 2.28 the release toolchain links against.
target_link_libraries(vice_c64 PRIVATE vice_c64_lib ${CMAKE_DL_LIBS})
target_include_directories(vice_c64 PRIVATE
    upstream
    upstream/libretro
    upstream/libretro-common/include
    upstream/vice/src)
# dladdr, which finds the data/ folder beside the plugin.
target_compile_definitions(vice_c64 PRIVATE _GNU_SOURCE)
target_compile_options(vice_c64 PRIVATE
    -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers)

# Commodore's ROMs are fetched, never committed, so dropping them stays a build change. roms.sha256
# lists every .bin under vice/data/C64 and vice/data/DRIVES at this revision. See README.md.
set(VICE_ROM_REVISION e9f8ac034ddef3025f0567768f7af8219f7cfdb8)
set(VICE_ROM_URL "https://raw.githubusercontent.com/libretro/vice-libretro/${VICE_ROM_REVISION}/vice/data")
# Outside the per-config build tree, so --clean and every config share one offline copy.
set(VICE_ROM_CACHE "${CMAKE_SOURCE_DIR}/build/downloads/vice_c64/${VICE_ROM_REVISION}")

include(FetchVerified)
fetch_verified("${CMAKE_CURRENT_SOURCE_DIR}/roms.sha256" "${VICE_ROM_URL}" "${VICE_ROM_CACHE}" VICE_ROMS)
set(VICE_C64_ROMS ${VICE_ROMS})
set(VICE_DRIVE_ROMS ${VICE_ROMS})
list(FILTER VICE_C64_ROMS INCLUDE REGEX "/C64/[^/]+$")
list(FILTER VICE_DRIVE_ROMS INCLUDE REGEX "/DRIVES/[^/]+$")

# VICE looks in <system directory>/vice/, and the plugin points the system directory at data/.
set(VICE_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)
set(VICE_DATA_STAMP ${CMAKE_CURRENT_BINARY_DIR}/data.stamp)
add_custom_command(
    OUTPUT ${VICE_DATA_STAMP}
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${VICE_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VICE_DATA_DIR}/vice/C64 ${VICE_DATA_DIR}/vice/DRIVES
    COMMAND ${CMAKE_COMMAND} -E copy ${VICE_C64_ROMS} ${VICE_DATA_DIR}/vice/C64
    COMMAND ${CMAKE_COMMAND} -E copy ${VICE_DRIVE_ROMS} ${VICE_DATA_DIR}/vice/DRIVES
    COMMAND ${CMAKE_COMMAND} -E touch ${VICE_DATA_STAMP}
    DEPENDS ${VICE_C64_ROMS} ${VICE_DRIVE_ROMS} roms.sha256 ${CMAKE_CURRENT_LIST_FILE}
    VERBATIM
)
add_custom_target(vice_c64_data DEPENDS ${VICE_DATA_STAMP})
add_dependencies(vice_c64 vice_c64_data)

# VICE and libretro-common define names the host also exports -- log_message is the one that bit --
# and without this the dynamic loader would bind VICE's internal calls to the host's versions.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(vice_c64 PRIVATE -Wl,-Bsymbolic)
endif()
