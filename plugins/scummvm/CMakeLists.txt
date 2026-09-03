# ScummVM Core Plugin
#
# Native OSystem backend integration using libco coroutines
# This builds ScummVM with all engines as a Replay Frontend core plugin

cmake_minimum_required(VERSION 3.16)

enable_language(CXX)

# ScummVM requires C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Paths
set(SCUMMVM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/upstream)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

# Create generated directory
file(MAKE_DIRECTORY ${GENERATED_DIR}/engines)

#######################################################################
# Generate engine configuration files
#######################################################################

# Run the engine generation script at configure time
message(STATUS "Generating ScummVM engine configuration...")
execute_process(
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/generate_engines.sh
            ${SCUMMVM_DIR} ${GENERATED_DIR}
    WORKING_DIRECTORY ${SCUMMVM_DIR}
    RESULT_VARIABLE ENGINE_GEN_RESULT
    OUTPUT_VARIABLE ENGINE_GEN_OUTPUT
    ERROR_VARIABLE ENGINE_GEN_ERROR
)

if(NOT ENGINE_GEN_RESULT EQUAL 0)
    message(WARNING "Engine generation returned: ${ENGINE_GEN_RESULT}")
    message(STATUS "Output: ${ENGINE_GEN_OUTPUT}")
    message(STATUS "Error: ${ENGINE_GEN_ERROR}")
    # Don't fail - we'll create stub files if needed
endif()

# Check if plugin tables were generated, create stubs if not
if(NOT EXISTS ${GENERATED_DIR}/engines/plugins_table.h)
    message(STATUS "Creating stub plugins_table.h")
    file(WRITE ${GENERATED_DIR}/engines/plugins_table.h
        "// Auto-generated stub - engine generation script didn't run\n"
        "// Add LINK_PLUGIN entries here\n"
    )
endif()

if(NOT EXISTS ${GENERATED_DIR}/engines/detection_table.h)
    message(STATUS "Creating stub detection_table.h")
    file(WRITE ${GENERATED_DIR}/engines/detection_table.h
        "// Auto-generated stub - engine generation script didn't run\n"
        "// Add LINK_PLUGIN entries here\n"
    )
endif()

#######################################################################
# Build libco as a static library
#######################################################################

add_library(scummvm_libco STATIC
    libco.c
)
target_include_directories(scummvm_libco PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
# Disable inline ASM - it doesn't handle TLS correctly in shared libraries
# The RIP-relative addressing used doesn't work for __thread variables in .so files
target_compile_definitions(scummvm_libco PRIVATE
    LIBCO_NO_INLINE_ASM=1
)
set_target_properties(scummvm_libco PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 11
)

#######################################################################
# Build libFLAC decoder (bundled, decoder-only)
#######################################################################

add_subdirectory(external/libflac)

#######################################################################
# Collect ScummVM source files
#######################################################################

# Core ScummVM sources
file(GLOB_RECURSE SCUMMVM_BASE_SOURCES ${SCUMMVM_DIR}/base/*.cpp)
file(GLOB_RECURSE SCUMMVM_COMMON_SOURCES ${SCUMMVM_DIR}/common/*.cpp)
file(GLOB_RECURSE SCUMMVM_GRAPHICS_SOURCES ${SCUMMVM_DIR}/graphics/*.cpp)
file(GLOB_RECURSE SCUMMVM_IMAGE_SOURCES ${SCUMMVM_DIR}/image/*.cpp)
# Exclude image codecs that need external libraries
list(FILTER SCUMMVM_IMAGE_SOURCES EXCLUDE REGEX ".*mpeg\\.cpp$")
list(FILTER SCUMMVM_IMAGE_SOURCES EXCLUDE REGEX ".*gif\\.cpp$")
file(GLOB_RECURSE SCUMMVM_AUDIO_SOURCES ${SCUMMVM_DIR}/audio/*.cpp)
# Exclude hardware-specific audio files that need external libraries
list(FILTER SCUMMVM_AUDIO_SOURCES EXCLUDE REGEX ".*opl2lpt\\.cpp$")
list(FILTER SCUMMVM_AUDIO_SOURCES EXCLUDE REGEX ".*rwopl3\\.cpp$")
list(FILTER SCUMMVM_AUDIO_SOURCES EXCLUDE REGEX ".*alsa_opl\\.cpp$")
# Exclude audio decoders that need external libraries
list(FILTER SCUMMVM_AUDIO_SOURCES EXCLUDE REGEX ".*/decoders/aac\\.cpp$")
list(FILTER SCUMMVM_AUDIO_SOURCES EXCLUDE REGEX ".*/decoders/ac3\\.cpp$")
# FLAC is enabled - see USE_FLAC below
list(FILTER SCUMMVM_AUDIO_SOURCES EXCLUDE REGEX ".*/decoders/mp3\\.cpp$")
list(FILTER SCUMMVM_AUDIO_SOURCES EXCLUDE REGEX ".*/decoders/mpc\\.cpp$")
list(FILTER SCUMMVM_AUDIO_SOURCES EXCLUDE REGEX ".*/decoders/vorbis\\.cpp$")
# Exclude MT-32 srchelper files that need external resampling libraries
list(FILTER SCUMMVM_AUDIO_SOURCES EXCLUDE REGEX ".*/mt32/srchelper/SamplerateAdapter\\.cpp$")
list(FILTER SCUMMVM_AUDIO_SOURCES EXCLUDE REGEX ".*/mt32/srchelper/SoxrAdapter\\.cpp$")
file(GLOB_RECURSE SCUMMVM_GUI_SOURCES ${SCUMMVM_DIR}/gui/*.cpp)
# Exclude GUI files that use event recorder or update manager
list(FILTER SCUMMVM_GUI_SOURCES EXCLUDE REGEX ".*recorderdialog\\.cpp$")
list(FILTER SCUMMVM_GUI_SOURCES EXCLUDE REGEX ".*onscreendialog\\.cpp$")
list(FILTER SCUMMVM_GUI_SOURCES EXCLUDE REGEX ".*updates-dialog\\.cpp$")
file(GLOB_RECURSE SCUMMVM_VIDEO_SOURCES ${SCUMMVM_DIR}/video/*.cpp)
# Exclude video decoders that need external libraries (theora, vorbis, etc.)
list(FILTER SCUMMVM_VIDEO_SOURCES EXCLUDE REGEX ".*hnm_decoder\\.cpp$")
list(FILTER SCUMMVM_VIDEO_SOURCES EXCLUDE REGEX ".*mkv_decoder\\.cpp$")
list(FILTER SCUMMVM_VIDEO_SOURCES EXCLUDE REGEX ".*theora_decoder\\.cpp$")
file(GLOB_RECURSE SCUMMVM_MATH_SOURCES ${SCUMMVM_DIR}/math/*.cpp)

# Exclude platform-specific files from common
list(FILTER SCUMMVM_COMMON_SOURCES EXCLUDE REGEX ".*updates\\.cpp$")
list(FILTER SCUMMVM_COMMON_SOURCES EXCLUDE REGEX ".*recorderfile\\.cpp$")

# Exclude platform-specific and include-only files from graphics
list(FILTER SCUMMVM_GRAPHICS_SOURCES EXCLUDE REGEX ".*blit-atari\\.cpp$")
list(FILTER SCUMMVM_GRAPHICS_SOURCES EXCLUDE REGEX ".*atari.*\\.cpp$")
# This file is #included in larryScale.cpp, not compiled standalone
list(FILTER SCUMMVM_GRAPHICS_SOURCES EXCLUDE REGEX ".*larryScale_generated\\.cpp$")

# Architecture-specific SIMD files - exclude based on target architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64|ARM64")
    # ARM: exclude x86 SIMD files
    list(FILTER SCUMMVM_GRAPHICS_SOURCES EXCLUDE REGEX ".*blit-sse2\\.cpp$")
    list(FILTER SCUMMVM_GRAPHICS_SOURCES EXCLUDE REGEX ".*blit-avx2\\.cpp$")
else()
    # x86/x64: exclude ARM SIMD files
    list(FILTER SCUMMVM_GRAPHICS_SOURCES EXCLUDE REGEX ".*blit-neon\\.cpp$")
endif()

# Backend base sources (only portable/default implementations)
file(GLOB SCUMMVM_BACKENDS_BASE ${SCUMMVM_DIR}/backends/*.cpp)
file(GLOB SCUMMVM_BACKENDS_AUDIOCD_BASE ${SCUMMVM_DIR}/backends/audiocd/*.cpp)
file(GLOB SCUMMVM_BACKENDS_AUDIOCD_DEFAULT ${SCUMMVM_DIR}/backends/audiocd/default/*.cpp)
file(GLOB SCUMMVM_BACKENDS_EVENTS ${SCUMMVM_DIR}/backends/events/default/*.cpp)
file(GLOB SCUMMVM_BACKENDS_FS_POSIX ${SCUMMVM_DIR}/backends/fs/posix/*.cpp)
file(GLOB SCUMMVM_BACKENDS_FS_ABSTRACT ${SCUMMVM_DIR}/backends/fs/abstract-fs.cpp)
file(GLOB SCUMMVM_BACKENDS_FS_STDIO ${SCUMMVM_DIR}/backends/fs/stdiostream.cpp)
file(GLOB SCUMMVM_BACKENDS_KEYMAPPER ${SCUMMVM_DIR}/backends/keymapper/*.cpp)
file(GLOB SCUMMVM_BACKENDS_LOG ${SCUMMVM_DIR}/backends/log/*.cpp)
file(GLOB SCUMMVM_BACKENDS_MIXER ${SCUMMVM_DIR}/backends/mixer/*.cpp)
file(GLOB SCUMMVM_BACKENDS_MUTEX_NULL ${SCUMMVM_DIR}/backends/mutex/null/*.cpp)
file(GLOB SCUMMVM_BACKENDS_SAVES_BASE ${SCUMMVM_DIR}/backends/saves/*.cpp)
file(GLOB SCUMMVM_BACKENDS_SAVES_DEFAULT ${SCUMMVM_DIR}/backends/saves/default/*.cpp)
file(GLOB SCUMMVM_BACKENDS_TIMER ${SCUMMVM_DIR}/backends/timer/default/*.cpp)
# Skip cloud/dlc/networking/tts backends for now - they need curl and other dependencies

# Engine sources - start with a curated list of well-tested engines
# (globbing all engines picks up unused/broken files not in module.mk)
set(SCUMMVM_ENGINE_SOURCES "")
set(ENGINE_NAMES "")

# Core engines - well tested, minimal dependencies
set(ENABLED_ENGINES
    # LucasArts / Revolution
    sky         # Beneath a Steel Sky
    queen       # Flight of the Amazon Queen
    scumm       # LucasArts SCUMM games (base + HE + 7/8)
    sword1      # Broken Sword 1
    sword2      # Broken Sword 2
    grim        # Grim Fandango, Escape from Monkey Island (3D)

    # Sierra
    agi         # Sierra AGI (King's Quest 1-4, Space Quest 1-2, LSL1)
    sci         # Sierra SCI (KQ5+, SQ3+, Gabriel Knight, Phantasmagoria)
    tsage       # Ringworld 1 & 2, Blue Force

    # Myst / Zork series
    mohawk      # Myst, Riven, Myst: Masterpiece Edition
    # zvision   # Zork Nemesis, Zork Grand Inquisitor (needs USE_FREETYPE)
    made        # Return to Zork

    # Other classic adventures
    agos        # Simon the Sorcerer series
    kyra        # Legend of Kyrandia series
    cine        # Future Wars, Operation Stealth
    lure        # Lure of the Temptress
    touche      # Touche: The Adventures of the Fifth Musketeer
    drascula    # Drascula: The Vampire Strikes Back
    hugo        # Hugo series
    tucker      # Bud Tucker in Double Trouble
    teenagent   # Teenagent
    gnap        # Gnap
    gob         # Gobliiins series
    saga        # Inherit the Earth, I Have No Mouth
    tinsel      # Discworld 1 & 2
    groovie     # The 7th Guest, The 11th Hour
    dreamweb    # DreamWeb
    neverhood   # The Neverhood
    sherlock    # Sherlock Holmes series
    toon        # Toonstruck
    cruise      # Cruise for a Corpse
    composer    # Magic Tales series
    draci       # Dragon History

    # Notable / Cult classics
    bladerunner # Blade Runner
    startrek    # Star Trek 25th Anniversary, Judgment Rites
    asylum      # Sanitarium
    awe         # Another World / Out of This World
    darkseed    # Dark Seed 1 & 2
    macventure  # Shadowgate, Deja Vu, Uninvited
    # titanic   # Starship Titanic (Douglas Adams) - has many stub files not in module.mk
    dm          # Dungeon Master, Chaos Strikes Back
    lastexpress # The Last Express

    # Good additions
    tony        # Tony Tough
    toltecs     # 3 Skulls of the Toltecs
    hopkins     # Hopkins FBI
    bbvs        # Beavis and Butt-Head in Virtual Stupidity
    voyeur      # Voyeur 1 & 2
    access      # Amazon, Martian Memorandum
    mads        # Rex Nebular, Dragonsphere
    pegasus     # Journeyman Project: Pegasus Prime
)

# Sub-engines that we need to enable for full support
set(ENGINE_SUB_DEFINES
    # SCUMM sub-engines
    ENABLE_SCUMM_7_8=1      # v7/v8 games (The Dig, Full Throttle, CMI)
    ENABLE_HE=1             # Humongous Entertainment (Putt-Putt, Spy Fox)

    # SCI sub-engines
    ENABLE_SCI32=1          # 32-bit SCI (Gabriel Knight, Phantasmagoria)

    # Groovie sub-engines
    ENABLE_GROOVIE2=1       # The 11th Hour, Clandestiny

    # Mohawk sub-engines (Myst/Riven)
    ENABLE_MYST=1           # Myst, Myst: Masterpiece Edition
    ENABLE_RIVEN=1          # Riven: The Sequel to Myst
    ENABLE_CSTIME=1         # Where in Time is Carmen Sandiego?

    # AGOS sub-engines
    ENABLE_AGOS2=1          # Simon the Sorcerer 2, Feeble Files

    # Kyra sub-engines
    ENABLE_LOL=1            # Lands of Lore
    ENABLE_EOB=1            # Eye of the Beholder 1 & 2

    # SAGA sub-engines
    ENABLE_IHNM=1           # I Have No Mouth, and I Must Scream

    # TsAGE sub-engines
    ENABLE_RINGWORLD=1      # Ringworld: Revenge of the Patriarch
    ENABLE_RINGWORLD2=1     # Return to Ringworld
    ENABLE_BLUEFORCE=1      # Blue Force
)

foreach(ENGINE ${ENABLED_ENGINES})
    set(ENGINE_DIR ${SCUMMVM_DIR}/engines/${ENGINE})
    if(EXISTS ${ENGINE_DIR}/module.mk)
        list(APPEND ENGINE_NAMES ${ENGINE})
        file(GLOB_RECURSE ENGINE_SRCS ${ENGINE_DIR}/*.cpp)
        list(APPEND SCUMMVM_ENGINE_SOURCES ${ENGINE_SRCS})
    else()
        message(WARNING "Engine ${ENGINE} not found at ${ENGINE_DIR}")
    endif()
endforeach()

# Exclude SCUMM files that require optional dependencies (USE_ENET, USE_BASIC_NET)
list(FILTER SCUMMVM_ENGINE_SOURCES EXCLUDE REGEX ".*/scumm/he/net/.*\\.cpp$")
list(FILTER SCUMMVM_ENGINE_SOURCES EXCLUDE REGEX ".*/scumm/dialog-createsession\\.cpp$")
list(FILTER SCUMMVM_ENGINE_SOURCES EXCLUDE REGEX ".*/scumm/dialog-sessionselector\\.cpp$")

# Exclude template/example files not meant to be compiled
list(FILTER SCUMMVM_ENGINE_SOURCES EXCLUDE REGEX ".*/_template\\.cpp$")

# Exclude incomplete/stub files not in module.mk
# (none currently - engines with many stubs are disabled)

message(STATUS "Enabled ScummVM engines: ${ENGINE_NAMES}")

# Engine base sources (not in subdirectories)
file(GLOB SCUMMVM_ENGINES_BASE ${SCUMMVM_DIR}/engines/*.cpp)

message(STATUS "Found ${CMAKE_CURRENT_LIST_LENGTH} ScummVM engines")

#######################################################################
# Create ScummVM static library
#######################################################################

add_library(scummvm_lib STATIC
    # Core
    ${SCUMMVM_BASE_SOURCES}
    ${SCUMMVM_COMMON_SOURCES}
    ${SCUMMVM_GRAPHICS_SOURCES}
    ${SCUMMVM_IMAGE_SOURCES}
    ${SCUMMVM_AUDIO_SOURCES}
    ${SCUMMVM_GUI_SOURCES}
    ${SCUMMVM_VIDEO_SOURCES}
    ${SCUMMVM_MATH_SOURCES}
    # Backends
    ${SCUMMVM_BACKENDS_BASE}
    ${SCUMMVM_BACKENDS_AUDIOCD_BASE}
    ${SCUMMVM_BACKENDS_AUDIOCD_DEFAULT}
    ${SCUMMVM_BACKENDS_EVENTS}
    ${SCUMMVM_BACKENDS_FS_POSIX}
    ${SCUMMVM_BACKENDS_FS_ABSTRACT}
    ${SCUMMVM_BACKENDS_FS_STDIO}
    ${SCUMMVM_BACKENDS_KEYMAPPER}
    ${SCUMMVM_BACKENDS_LOG}
    ${SCUMMVM_BACKENDS_MIXER}
    ${SCUMMVM_BACKENDS_MUTEX_NULL}
    ${SCUMMVM_BACKENDS_SAVES_BASE}
    ${SCUMMVM_BACKENDS_SAVES_DEFAULT}
    ${SCUMMVM_BACKENDS_TIMER}
    # Engines base
    ${SCUMMVM_ENGINES_BASE}
    # All engine sources
    ${SCUMMVM_ENGINE_SOURCES}
    # Our custom backend
    replay_osystem.cpp
    replay_graphics.cpp
    replay_mixer.cpp
    replay_fs.cpp
)

target_include_directories(scummvm_lib PUBLIC
    # Our directory first (for config.h, portdefs.h)
    ${CMAKE_CURRENT_SOURCE_DIR}
    # Generated files (for plugins_table.h, detection_table.h)
    ${GENERATED_DIR}
    # ScummVM directories
    ${SCUMMVM_DIR}
    ${SCUMMVM_DIR}/engines
    ${SCUMMVM_DIR}/backends
    # Bundled libFLAC headers
    ${CMAKE_CURRENT_SOURCE_DIR}/external/libflac/include
)

# Generate ENABLE_* defines for all engines
set(ENGINE_DEFINES "")
foreach(ENGINE ${ENGINE_NAMES})
    string(TOUPPER ${ENGINE} ENGINE_UPPER)
    list(APPEND ENGINE_DEFINES "ENABLE_${ENGINE_UPPER}=1")
endforeach()

target_compile_definitions(scummvm_lib PUBLIC
    # Config
    HAVE_CONFIG_H=1
    NONSTANDARD_PORT=1
    DETECTION_STATIC=1

    # Platform
    POSIX=1
    UNIX=1

    # Features
    USE_RGB_COLOR=1
    USE_OSD=1
    USE_TRANSLATION=1
    USE_BINK=1
    USE_TINYGL=1
    USE_LUA=1
    USE_LIBCO=1
    USE_FLAC=1

    # Disable some features we don't need yet
    DISABLE_TEXT_CONSOLE=1

    # Size defines
    $<$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>:SIZEOF_SIZE_T=8>
    $<$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>:SCUMM_64BITS>
    $<$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>:SIZEOF_SIZE_T=4>

    # x86_64 SIMD support (SCUMMVM_ prefix for header conditionals)
    $<$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},x86_64>:SCUMMVM_SSE2>
    $<$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},AMD64>:SCUMMVM_SSE2>
    $<$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},x86_64>:SCUMMVM_AVX2>
    $<$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},AMD64>:SCUMMVM_AVX2>

    # Suppress some warnings in ScummVM code
    FORBIDDEN_SYMBOL_ALLOW_ALL=1

    # All engine enables
    ${ENGINE_DEFINES}

    # Sub-engine enables (SCUMM, SCI, etc.)
    ${ENGINE_SUB_DEFINES}
)

target_compile_options(scummvm_lib PRIVATE
    -Wno-shadow
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wno-sign-compare
    -Wno-switch
    -Wno-reorder
    -Wno-parentheses
    -Wno-overloaded-virtual
    -Wno-multichar
    -Wno-unknown-pragmas
    -Wno-long-long
    -Wno-undefined-var-template
    -Wno-pragma-pack
)

# Set special compiler flags for SIMD source files on x86_64
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set_source_files_properties(
        ${SCUMMVM_DIR}/graphics/blit/blit-sse2.cpp
        PROPERTIES COMPILE_FLAGS "-msse2"
    )
    set_source_files_properties(
        ${SCUMMVM_DIR}/graphics/blit/blit-avx2.cpp
        PROPERTIES COMPILE_FLAGS "-mavx2"
    )
endif()

set_target_properties(scummvm_lib PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
)

#######################################################################
# Create core plugin shared library
#######################################################################

add_replay_emu_plugin(NAME scummvm SOURCES scummvm_core.cpp)

target_link_libraries(scummvm PRIVATE
    scummvm_lib
    scummvm_libco
    flac_decoder
)

# Cross-compilation needs explicit zlib linkage (sysroot headers removed)
if(CMAKE_CROSSCOMPILING)
    find_package(ZLIB REQUIRED)
    target_include_directories(scummvm_lib PUBLIC ${ZLIB_INCLUDE_DIRS})
    target_link_libraries(scummvm PRIVATE ZLIB::ZLIB)
endif()

target_include_directories(scummvm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SCUMMVM_DIR}
)

target_compile_definitions(scummvm PRIVATE
    HAVE_CONFIG_H=1
    NONSTANDARD_PORT=1
)

target_compile_options(scummvm PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wno-shadow
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-missing-field-initializers
)

set_target_properties(scummvm PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)
