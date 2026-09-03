# vAmiga, built as the Amiga plugin. Only the emulator's Core/ is carried in-tree; see README.md.

# vAmiga is C++20 -- concepts and <latch>.
enable_language(CXX)

# vAmiga's own CMakeLists calls project(); this tells it that it is not the top of a build.
set(VAMIGA_AS_SUBPROJECT ON)
add_subdirectory(upstream/Core "${CMAKE_CURRENT_BINARY_DIR}/vamiga_build" EXCLUDE_FROM_ALL)

# All of it ends up inside a shared object, so all of it has to be relocatable.
foreach(target VACore xdms lz4)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()

# vAmiga's own warning flags are its business, not something to inflict on what links it.
set_target_properties(VACore PROPERTIES INTERFACE_COMPILE_OPTIONS "")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # STFile::decodeDisk builds a ~5.5 MB stack frame, which trips the global frame-size warning.
    target_compile_options(VACore PRIVATE -Wno-frame-larger-than)
    # GCC 16 added -Wsfinae-incomplete, which vAmiga trips wherever a class is named in a concept
    # check before it is defined. vAmiga builds itself with -Werror, so the new warning is fatal.
    # The frontend never saw this because it builds with clang, which has no such warning.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
        target_compile_options(VACore PRIVATE -Wno-sfinae-incomplete)
    endif()
    # libstdc++ 16 dropped transitive includes vAmiga relied on for INT_MAX and the fixed-width
    # integer types. Forcing them in builds the emulator on newer standard libraries without
    # patching it. The C++ spellings are used for C++ translation units because the C <*.h> forms
    # trip clang's -Wdeprecated under -Werror.
    target_compile_options(VACore PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cstdint -include climits>"
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include stdint.h -include limits.h>")
endif()

# tlsf.c is vendored beside the plugin rather than reached for inside flowi: hle_rom.c runs a heap
# inside the emulated Amiga's own RAM to answer AllocMem and FreeMem, and a plugin has no access to
# the host's internal sources.
add_replay_emu_plugin(NAME vamiga SOURCES vamiga_core.cpp hle_rom.c tlsf.c)
target_link_libraries(vamiga PRIVATE VACore)
target_include_directories(vamiga PRIVATE . upstream/Core)
target_compile_options(vamiga PRIVATE
    -Wall -Wextra -Werror
    -Wno-shadow -Wno-undef -Wno-unused-parameter -Wno-missing-field-initializers)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
    # The wrapper includes vAmiga's headers, so it trips -Wsfinae-incomplete the same way VACore does.
    # C++ only: the wrapper also compiles hle_rom.c and tlsf.c, and the flag is rejected for C.
    target_compile_options(vamiga PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wno-sfinae-incomplete>")
endif()
set_target_properties(vamiga PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)
