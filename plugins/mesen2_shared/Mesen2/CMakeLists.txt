# Mesen2 Core Libraries for Replay Frontend
# Builds the emulation core without SDL/platform-specific code

cmake_minimum_required(VERSION 3.16)

# This is included as a subdirectory, don't override parent project settings
if(NOT CMAKE_PROJECT_NAME)
    project(Mesen2Core LANGUAGES C CXX)
endif()

# C++17 is required for Mesen2
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Position-independent code for shared library linking
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# =============================================================================
# SevenZip Library (LZMA decompression)
# =============================================================================

file(GLOB SEVENZIP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SevenZip/*.c")

add_library(MesenSevenZip STATIC ${SEVENZIP_SOURCES})

target_include_directories(MesenSevenZip PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/SevenZip
)

# Suppress all warnings for third-party code
target_compile_options(MesenSevenZip PRIVATE -w)

# =============================================================================
# Utilities Library
# =============================================================================

file(GLOB_RECURSE UTILITIES_CPP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/*.cpp")
file(GLOB_RECURSE UTILITIES_C_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/*.c")

add_library(MesenUtilities STATIC
    ${UTILITIES_CPP_SOURCES}
    ${UTILITIES_C_SOURCES}
)

target_include_directories(MesenUtilities PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Utilities
)

target_link_libraries(MesenUtilities PUBLIC MesenSevenZip)

# Suppress warnings for external code
target_compile_options(MesenUtilities PRIVATE -w)

# =============================================================================
# Core Library (All emulation engines)
# =============================================================================

# Note: We compile all systems because Shared/Emulator.cpp references all console
# types via TryLoadRom<T> template instantiations. Stubbing individual consoles
# is impractical due to unique_ptr member destructor requirements.
file(GLOB_RECURSE CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.cpp")

# Exclude pch.cpp if present (precompiled header stub)
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*pch\\.cpp$")

# Exclude Lua-related debugger files (requires Lua library which we don't build)
# ScriptManagerStub.cpp in the plugin provides empty implementations
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/Debugger/LuaApi\\.cpp$")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/Debugger/LuaCallHelper\\.cpp$")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/Debugger/ScriptingContext\\.cpp$")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/Debugger/ScriptHost\\.cpp$")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/Debugger/ScriptManager\\.cpp$")

add_library(MesenCore STATIC ${CORE_SOURCES})

target_include_directories(MesenCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/Utilities
)

target_link_libraries(MesenCore PUBLIC
    MesenUtilities
    MesenSevenZip
)

# Suppress warnings for external code
target_compile_options(MesenCore PRIVATE -w)

# Platform-specific threading
find_package(Threads REQUIRED)
target_link_libraries(MesenCore PUBLIC Threads::Threads)

# C++17 filesystem (needed on older compilers)
if(NOT APPLE)
    target_link_libraries(MesenCore PUBLIC stdc++fs)
endif()

# =============================================================================
# Compiler definitions
# =============================================================================

# Define platform macros that Mesen2 expects
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(MesenCore PRIVATE __linux__)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_compile_definitions(MesenCore PRIVATE __APPLE__)
elseif(WIN32)
    target_compile_definitions(MesenCore PRIVATE _WIN32)
endif()
