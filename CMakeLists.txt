cmake_minimum_required(VERSION 3.16)
project(replay_frontend_plugins C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(ReplayPlugins)

if(NOT REPLAY_PLUGINS)
    message(FATAL_ERROR
        "No plugins selected. Use ./build.sh <plugin>..., or configure by hand with "
        "-DREPLAY_PLUGINS=<name>[;<name>...].")
endif()

# An unselected plugin's CMakeLists never runs, and its submodule is never initialised.
foreach(plugin IN LISTS REPLAY_PLUGINS)
    if(NOT IS_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/plugins/${plugin}")
        message(FATAL_ERROR "No such plugin: '${plugin}' (expected plugins/${plugin})")
    endif()
    # Directory-scoped, so each plugin's shared object lands under build/<config>/plugins/<name>/,
    # which is the layout ./build.sh --deploy copies from.
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${plugin}")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${plugin}")
    add_subdirectory("plugins/${plugin}")
endforeach()

# The smoke host and its fake plugins. Built with everything, so ./build.sh --smoke is always there
# and the fakes never go stale beside the host they test.
add_subdirectory(smoke)
