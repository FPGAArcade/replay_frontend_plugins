#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The declarative half of a smoke run: what smoke.toml says, and the provenance record every
// fixture must ship with.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <flowi/core/types.h>

#define SMOKE_PATH_MAX 1024

typedef struct SmokeConfig {
    // Absolute path to the fixture the plugin mounts, resolved against the smoke.toml's directory.
    // Empty when smoke.toml names no fixture, which asks the plugin to boot with nothing mounted.
    char fixture[SMOKE_PATH_MAX];
    // How many frames to run before the assertions are made.
    u32 frames;
    // The watchdog: a run that has not finished by then is a hang, not a slow core.
    u32 timeout_seconds;
    // Whether the run also asserts that the core produced audio.
    bool audio;
} SmokeConfig;

// Reads a plugin's smoke.toml. False on a missing file, an unknown or malformed key, or a value
// out of range; the reason is on stderr.
bool smoke_config_read(const char* path, SmokeConfig* out_config);

// Checks that `fixture_path` exists and has a provenance record beside it naming a source and a
// licence. False, with the reason on stderr, when either is missing.
bool smoke_fixture_provenance_ok(const char* fixture_path);
