# Hatari, built from hatariB's libretro core.
#
# hatariB is not a library anyone packages, so this builds the whole stack from the pinned
# upstream: zlib and a stripped-down SDL2 at configure time, then hatari's own static libraries,
# then hatariB's libretro core, and finally the plugin that wraps it.

if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    message(STATUS "Skipping hatari: only Linux and macOS are supported")
    return()
endif()

set(HATARIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/upstream")
set(HATARI_DIR "${HATARIB_DIR}/hatari")
# In the source tree on purpose: hatari's main.h includes "../../core/core.h", which only resolves
# to hatariB's core/ when the build directory sits exactly two levels below upstream/. hatariB's
# own .gitignore covers this path, so building leaves the submodule clean.
set(HATARI_BUILD_DIR "${HATARI_DIR}/build")

if(NOT EXISTS "${HATARIB_DIR}/SDL/CMakeLists.txt")
    message(STATUS "Initialising hatariB's own submodules (SDL, zlib)...")
    execute_process(
        COMMAND git submodule update --init --depth 1
        WORKING_DIRECTORY "${HATARIB_DIR}"
        RESULT_VARIABLE SUBMODULE_RESULT)
    if(NOT SUBMODULE_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not fetch hatariB's submodules (exit ${SUBMODULE_RESULT})")
    endif()
endif()

# ---------------------------------------------------------------------------------------------
# zlib and SDL2, configured and built now rather than at build time: hatari's own CMake needs
# both present before it will configure.
# ---------------------------------------------------------------------------------------------

set(ZLIB_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/zlib_build")
set(ZLIB_STATIC_LIB "${ZLIB_BUILD_DIR}/lib/libz.a")

if(NOT EXISTS "${ZLIB_STATIC_LIB}")
    message(STATUS "Building zlib for hatariB...")
    file(MAKE_DIRECTORY "${ZLIB_BUILD_DIR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env "CFLAGS=-fPIC"
            "${HATARIB_DIR}/zlib/configure" --static --prefix=${ZLIB_BUILD_DIR}
        WORKING_DIRECTORY "${ZLIB_BUILD_DIR}"
        RESULT_VARIABLE ZLIB_CONF_RESULT
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT ZLIB_CONF_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not configure zlib for hatariB")
    endif()
    execute_process(COMMAND make -j static WORKING_DIRECTORY "${ZLIB_BUILD_DIR}" OUTPUT_QUIET ERROR_QUIET)
    execute_process(COMMAND make install WORKING_DIRECTORY "${ZLIB_BUILD_DIR}" OUTPUT_QUIET ERROR_QUIET)
    if(NOT EXISTS "${ZLIB_STATIC_LIB}")
        message(FATAL_ERROR "Could not build zlib for hatariB")
    endif()
endif()

set(SDL2_BUILD_DIR "${HATARIB_DIR}/SDL/build")
set(SDL2_STATIC_LIB "${SDL2_BUILD_DIR}/lib/libSDL2.a")

if(NOT EXISTS "${SDL2_STATIC_LIB}")
    message(STATUS "Building minimal SDL2 for hatariB...")
    file(MAKE_DIRECTORY "${SDL2_BUILD_DIR}")
    # hatari links SDL2 but the plugin never opens a window, a device or a file through it: the
    # frontend owns all of that. Everything SDL2 can be built without is switched off here, which
    # is also what keeps the plugin from pulling in X11, Wayland and the GPU stacks.
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env "CFLAGS=-fPIC"
            "${HATARIB_DIR}/SDL/configure" --disable-shared --prefix=${SDL2_BUILD_DIR}
            --enable-audio=no --enable-power=no --enable-filesystem=no --enable-timers=no --enable-file=no
            --enable-events=no --enable-render=no --enable-loadso=no --enable-cpuinfo=no --enable-assembly=no
            --enable-atomic=no --enable-system-iconv=no --enable-misc=no --enable-locale=no
            --enable-joystick=no --enable-haptic=no --enable-hidapi=no --enable-sensor=no
            --enable-video-wayland=no --enable-video-rpi=no --enable-video-x11=no
            --enable-video-vivante=no --enable-video-cocoa=no --enable-video-metal=no
            --enable-render-metal=no --enable-video-directfb=no --enable-video-kmsdrm=no
            --enable-video-opengl=no --enable-video-opengles=no --enable-video-opengles1=no
            --enable-video-opengles2=no --enable-video-vulkan=no --enable-render-d3d=no
            --enable-werror=no
        WORKING_DIRECTORY "${SDL2_BUILD_DIR}"
        RESULT_VARIABLE SDL2_CONF_RESULT
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT SDL2_CONF_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not configure SDL2 for hatariB")
    endif()
    execute_process(COMMAND make -j WORKING_DIRECTORY "${SDL2_BUILD_DIR}" OUTPUT_QUIET ERROR_QUIET)
    execute_process(COMMAND make install WORKING_DIRECTORY "${SDL2_BUILD_DIR}" OUTPUT_QUIET ERROR_QUIET)
    if(NOT EXISTS "${SDL2_STATIC_LIB}")
        message(FATAL_ERROR "Could not build SDL2 for hatariB")
    endif()
endif()

# ---------------------------------------------------------------------------------------------
# Hatari itself: configured now, built as part of this build.
# ---------------------------------------------------------------------------------------------

if(NOT EXISTS "${HATARI_BUILD_DIR}/Makefile" AND NOT EXISTS "${HATARI_BUILD_DIR}/build.ninja")
    message(STATUS "Configuring hatari...")
    file(MAKE_DIRECTORY "${HATARI_BUILD_DIR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} "${HATARI_DIR}"
            -DCMAKE_C_FLAGS=-fPIC\ -D__LIBRETRO__
            -DZLIB_INCLUDE_DIR=${ZLIB_BUILD_DIR}/include
            -DZLIB_LIBRARY=${ZLIB_STATIC_LIB}
            -DSDL2_DIR=${SDL2_BUILD_DIR}/lib/cmake/SDL2
            -DCMAKE_DISABLE_FIND_PACKAGE_Readline=1
            -DCMAKE_DISABLE_FIND_PACKAGE_X11=1
            -DCMAKE_DISABLE_FIND_PACKAGE_PNG=1
            -DCMAKE_DISABLE_FIND_PACKAGE_PortMidi=1
            -DCMAKE_DISABLE_FIND_PACKAGE_CapsImage=1
            -DENABLE_SMALL_MEM=0
            -DENABLE_TRACING=0
            -DCMAKE_BUILD_TYPE=Release
        WORKING_DIRECTORY "${HATARI_BUILD_DIR}"
        RESULT_VARIABLE HATARI_CONF_RESULT)
    if(NOT HATARI_CONF_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not configure hatari")
    endif()
endif()

set(HATARI_LIBS
    "${HATARI_BUILD_DIR}/src/libcore.a"
    "${HATARI_BUILD_DIR}/src/falcon/libFalcon.a"
    "${HATARI_BUILD_DIR}/src/cpu/libUaeCpu.a"
    "${HATARI_BUILD_DIR}/src/gui-sdl/libGuiSdl.a"
    "${HATARI_BUILD_DIR}/src/libFloppy.a"
    "${HATARI_BUILD_DIR}/src/debug/libDebug.a")

add_custom_command(
    OUTPUT ${HATARI_LIBS}
    COMMAND ${CMAKE_COMMAND} --build . -j
    WORKING_DIRECTORY "${HATARI_BUILD_DIR}"
    COMMENT "Building hatari's static libraries")
add_custom_target(hatari_libs DEPENDS ${HATARI_LIBS})

# ---------------------------------------------------------------------------------------------
# hatariB's libretro core, and the plugin that drives it.
# ---------------------------------------------------------------------------------------------

add_library(hatarib_core_wrapper STATIC
    "${HATARIB_DIR}/core/core.c"
    "${HATARIB_DIR}/core/core_file.c"
    "${HATARIB_DIR}/core/core_input.c"
    "${HATARIB_DIR}/core/core_disk.c"
    "${HATARIB_DIR}/core/core_config.c"
    "${HATARIB_DIR}/core/core_osk.c")
add_dependencies(hatarib_core_wrapper hatari_libs)

# hatariB stamps the build's git hash into its version string; this build is identified by the pin.
target_compile_definitions(hatarib_core_wrapper PRIVATE __LIBRETRO__ SHORTHASH="replay")

target_include_directories(hatarib_core_wrapper PRIVATE
    "${HATARI_BUILD_DIR}"
    "${SDL2_BUILD_DIR}/include/SDL2"
    "${HATARIB_DIR}/libretro"
    "${HATARIB_DIR}/core"
    "${HATARI_DIR}/src/includes")

target_compile_options(hatarib_core_wrapper PRIVATE
    -O3 -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare -Wno-missing-field-initializers)

set_target_properties(hatarib_core_wrapper PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)

add_replay_emu_plugin(NAME hatari SOURCES hatari_plugin.c)
add_dependencies(hatari hatari_libs)

target_include_directories(hatari PRIVATE
    "${HATARIB_DIR}/libretro"
    "${HATARIB_DIR}/core"
    "${HATARI_BUILD_DIR}"
    "${SDL2_BUILD_DIR}/include/SDL2"
    "${HATARI_DIR}/src/includes")

target_compile_definitions(hatari PRIVATE __LIBRETRO__)
target_compile_options(hatari PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

# libcore.a appears twice: hatari's static libraries reference each other in both directions and a
# single pass over them leaves symbols unresolved.
target_link_libraries(hatari PRIVATE
    hatarib_core_wrapper
    "${HATARI_BUILD_DIR}/src/libcore.a"
    "${HATARI_BUILD_DIR}/src/falcon/libFalcon.a"
    "${HATARI_BUILD_DIR}/src/cpu/libUaeCpu.a"
    "${HATARI_BUILD_DIR}/src/gui-sdl/libGuiSdl.a"
    "${HATARI_BUILD_DIR}/src/libFloppy.a"
    "${HATARI_BUILD_DIR}/src/debug/libDebug.a"
    "${HATARI_BUILD_DIR}/src/libcore.a"
    "${ZLIB_STATIC_LIB}"
    "${SDL2_STATIC_LIB}"
    samplerate
    m
    pthread
    ${CMAKE_DL_LIBS})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(hatari PRIVATE udev)
    # The plugin carries a whole emulator's worth of symbols. -Bsymbolic binds its internal calls
    # to its own definitions, so nothing in the host or another plugin can interpose on them.
    target_link_options(hatari PRIVATE -Wl,-Bsymbolic)
endif()

# Optional in hatari's own build, so link it only where the development library is installed.
find_library(CAPSTONE_LIBRARY capstone)
if(CAPSTONE_LIBRARY)
    target_link_libraries(hatari PRIVATE "${CAPSTONE_LIBRARY}")
endif()
