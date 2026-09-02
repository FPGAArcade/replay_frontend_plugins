# Shared build glue for every plugin in this repository.
#
# Resolves the SDK, applies the repository-wide build configuration, and pulls in the SDK's own
# ReplaySDK.cmake, which is where add_replay_emu_plugin comes from. A plugin's CMakeLists needs
# nothing beyond that one call.

# The SDK is a submodule pinned at a commit of FPGAArcade/replay_frontend_sdk. REPLAY_SDK_DIR names
# a different tree when ./build.sh --sdk-dir is used, and REPLAY_LOCAL_SDK says that it did.
if(NOT REPLAY_SDK_DIR)
    set(REPLAY_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk")
endif()
get_filename_component(REPLAY_SDK_DIR "${REPLAY_SDK_DIR}" ABSOLUTE)

if(NOT EXISTS "${REPLAY_SDK_DIR}/cmake/ReplaySDK.cmake")
    message(FATAL_ERROR
        "No SDK at '${REPLAY_SDK_DIR}'. Build through ./build.sh, which initialises the pinned "
        "submodule, or pass --sdk-dir <path to a staged SDK>.")
endif()

if(REPLAY_LOCAL_SDK)
    message(WARNING "[LOCAL SDK] building against ${REPLAY_SDK_DIR} instead of the pinned submodule")
endif()

include("${REPLAY_SDK_DIR}/cmake/ReplaySDK.cmake")

if(REPLAY_PLUGIN_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
