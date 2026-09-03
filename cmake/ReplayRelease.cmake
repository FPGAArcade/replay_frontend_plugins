# The release floor: the compile and link settings a published plugin must be built with,
# applied to everything in this repository rather than restated per plugin.
#
# The SDK already hides a plugin target's own C symbols and builds it PIC. What it cannot
# reach is everything a plugin drags in with it - an emulator upstream is usually a pile of
# static libraries, and those carry their own symbols and their own C++ runtime needs. These
# settings are directory-wide for that reason: they cover the upstream sources too.
#
# One setting from the equivalent playback file is deliberately absent. A Replay plugin links
# nothing of the host and leaves every fl_*/rp_* call undefined for the loader to bind at load
# time, so -Wl,-z,defs would reject a correctly built plugin. The SDK's check_plugin.sh is what
# audits those imports instead, since the link step structurally cannot.

# Hidden by default, so a plugin exports its entry point and nothing else. C is set here as
# well as by the SDK: the SDK's is a target property on the plugin itself, and this reaches
# the static libraries linked into it.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT WIN32)
    # The device runs GCC 12's libstdc++, older than the gcc-toolset-13 that builds this, so a
    # dynamic reference to it would resolve against a runtime too old to satisfy it - and only
    # on device, at load, long after every check here passed. Linking it in statically removes
    # the question. --exclude-libs,ALL then stops those static libraries re-exporting their
    # symbols out of the plugin, which would otherwise collide with the host's own copies. What
    # it cannot reach is what the plugin's own objects instantiate out of libstdc++'s headers,
    # and the version script is what localises those; see plugin_exports.map for why.
    string(APPEND CMAKE_SHARED_LINKER_FLAGS
        " -static-libstdc++ -static-libgcc"
        " -Wl,--exclude-libs,ALL"
        " -Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/plugin_exports.map")

    foreach(lang C CXX)
        # Build paths and timestamps are the two things that make an otherwise identical
        # rebuild differ, and byte-identical rebuilds are a release requirement. The first
        # maps absolute paths to fixed names; the second makes __DATE__ and __TIME__ a build
        # error rather than a silent source of drift.
        string(APPEND CMAKE_${lang}_FLAGS
            " -ffile-prefix-map=${CMAKE_SOURCE_DIR}=/rpsrc"
            " -ffile-prefix-map=${CMAKE_BINARY_DIR}=/rpbuild"
            " -Werror=date-time")
    endforeach()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        # The device's baseline. Anything above it would run on the developer's machine and
        # fault on the hardware.
        string(APPEND CMAKE_C_FLAGS " -march=armv8-a")
        string(APPEND CMAKE_CXX_FLAGS " -march=armv8-a")
    endif()
endif()
