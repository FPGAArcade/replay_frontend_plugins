# The Mesen2 family: seven plugins, one upstream, one build of it.
#
# Mesen2 is a single emulator that covers NES, SNES, Game Boy, Game Boy Advance, PC Engine, Master
# System and WonderSwan. Each system is a separate plugin because the frontend presents them as
# separate machines, but they all link the same three static libraries, so the upstream is
# configured and compiled once and every plugin after the first reuses it.
#
# The upstream is a pristine copy carried in-tree rather than a submodule: it is small, and we have
# no patches against it, so there is no series for a pin to reproduce.
#
# Each plugin is then one line in its own CMakeLists. Before this they were seven near-identical
# files that reached sideways into the NES directory for the shared stub, and a change to the build
# meant seven edits.

set(MESEN2_SHARED_DIR "${CMAKE_CURRENT_LIST_DIR}/../plugins/mesen2_shared")
get_filename_component(MESEN2_SHARED_DIR "${MESEN2_SHARED_DIR}" ABSOLUTE)
set(MESEN2_UPSTREAM_DIR "${MESEN2_SHARED_DIR}/Mesen2")

# Configures the Mesen2 upstream, once, however many of its plugins were selected.
function(_replay_mesen2_upstream)
    if(TARGET MesenCore)
        return()
    endif()

    # Out of the plugin's own output directory: these libraries belong to no single plugin.
    add_subdirectory("${MESEN2_UPSTREAM_DIR}" "${CMAKE_BINARY_DIR}/mesen2_shared" EXCLUDE_FROM_ALL)

    # Everything here ends up inside a shared object, so all of it has to be relocatable.
    foreach(target MesenCore MesenUtilities MesenSevenZip)
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endforeach()
endfunction()

# add_replay_mesen2_plugin(NAME mesen2_nes SOURCES mesen2_nes_core.cpp)
function(add_replay_mesen2_plugin)
    cmake_parse_arguments(MESEN "" "NAME" "SOURCES" ${ARGN})
    if(NOT MESEN_NAME OR NOT MESEN_SOURCES)
        message(FATAL_ERROR "add_replay_mesen2_plugin: NAME and SOURCES are required")
    endif()

    _replay_mesen2_upstream()

    # ScriptManagerStub.cpp answers Mesen2's Lua scripting entry points with empty implementations:
    # the frontend exposes no scripting, and without them the link fails on symbols nothing calls.
    add_replay_emu_plugin(NAME ${MESEN_NAME}
        SOURCES ${MESEN_SOURCES} "${MESEN2_SHARED_DIR}/ScriptManagerStub.cpp")

    target_link_libraries(${MESEN_NAME} PRIVATE MesenCore)
    target_include_directories(${MESEN_NAME} PRIVATE
        "${MESEN2_SHARED_DIR}"
        "${MESEN2_UPSTREAM_DIR}"
        "${MESEN2_UPSTREAM_DIR}/Core"
        "${MESEN2_UPSTREAM_DIR}/Utilities")
    target_compile_options(${MESEN_NAME} PRIVATE
        -Wall -Wextra -Werror -Wno-shadow -Wno-undef
        -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare
        -Wno-ignored-qualifiers)
    set_target_properties(${MESEN_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
endfunction()
