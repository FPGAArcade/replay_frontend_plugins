# The smoke host, and the fake plugins that prove it fails what it should.
#
# The host is an executable, not a plugin, so it uses the SDK's headers directly rather than
# add_replay_emu_plugin. ENABLE_EXPORTS puts its own symbols in the dynamic symbol table, which is
# how a plugin it loads binds its fl_*/arena_* calls to this process.

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(replay_smoke smoke_host.c smoke_config.c host_symbols.c)
set_target_properties(replay_smoke PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    ENABLE_EXPORTS ON)
target_include_directories(replay_smoke PRIVATE "${REPLAY_SDK_DIR}/include")
target_link_libraries(replay_smoke PRIVATE ${CMAKE_DL_LIBS})
target_compile_options(replay_smoke PRIVATE -Wall -Wextra)

foreach(fake hang blank abi noslot silent nomedia strings)
    string(TOUPPER "${fake}" fake_upper)
    add_replay_emu_plugin(NAME fake_${fake} SOURCES fakes/fake_plugin.c)
    target_compile_definitions(fake_${fake} PRIVATE FAKE_${fake_upper})
    target_compile_options(fake_${fake} PRIVATE -Wall -Wextra)
endforeach()
