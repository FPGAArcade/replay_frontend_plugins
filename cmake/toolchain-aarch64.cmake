# Cross-compiling a plugin for the device's aarch64, inside the toolchain image and nowhere else.
#
# clang is the compiler because the sysroot's own gcc-toolset-13 binaries are aarch64 executables
# that cannot run on the x86_64 host building them. What is used of that toolset is its headers,
# its libstdc++.a and its runtime bits, which --gcc-toolchain points clang at; lld links, because
# the sysroot's ld is an aarch64 binary for the same reason.
#
# The sysroot is Rocky 8's aarch64 package set - glibc 2.28, the same floor as the native side.
# The frontend's device sysroot is Debian 12 and is deliberately not used here: building against
# it would raise the arm64 floor to glibc 2.36 while every check in this repository still passed.
#
# Both paths come from the environment because the image is what defines them. Nothing outside it
# has this sysroot, so a bare-host cross build is a clear failure rather than a subtly wrong one.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED ENV{REPLAY_AARCH64_SYSROOT} OR NOT DEFINED ENV{REPLAY_AARCH64_GCC_TOOLCHAIN})
    message(FATAL_ERROR
        "REPLAY_AARCH64_SYSROOT and REPLAY_AARCH64_GCC_TOOLCHAIN are unset, so there is no "
        "aarch64 sysroot to build against. Cross builds run inside the toolchain image: "
        "./build.sh --docker --target aarch64 <plugin>")
endif()

set(CMAKE_SYSROOT "$ENV{REPLAY_AARCH64_SYSROOT}")
set(replay_gcc_toolchain "$ENV{REPLAY_AARCH64_GCC_TOOLCHAIN}")
set(replay_triple aarch64-unknown-linux-gnu)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET ${replay_triple})
set(CMAKE_CXX_COMPILER_TARGET ${replay_triple})
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_ASM_COMPILER_TARGET ${replay_triple})

foreach(lang C CXX ASM)
    set(CMAKE_${lang}_FLAGS_INIT "--gcc-toolchain=${replay_gcc_toolchain}")
endforeach()
# The release floor passes -static-libstdc++ to every link, and clang says so on a plugin with
# no C++ in it. gcc, which drives the native build, is silent about the same flag; the warning
# is about the driver rather than about the plugin, so it is turned off rather than worked
# around by making the floor conditional.
foreach(kind EXE SHARED MODULE)
    set(CMAKE_${kind}_LINKER_FLAGS_INIT
        "--gcc-toolchain=${replay_gcc_toolchain} -fuse-ld=lld -Wno-unused-command-line-argument")
endforeach()

# Programs come from the host - cmake, ninja, and clang itself are x86_64. Everything the build
# looks up to link or include against must come from the sysroot instead.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
