#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The host half of the plugin ABI.
//
// A plugin links nothing: its fl_*/arena_* calls stay undefined and the dynamic loader binds them
// to the process that loaded it. The frontend satisfies them from the real flowi; the smoke host
// satisfies them itself, which is what makes it SDK-only.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <flowi/core/types.h>

typedef struct FlArena FlArena;

// The arena passed to a plugin's create(). One per process, reserved on first use.
FlArena* smoke_host_arena(void);

// Whether plugin log output is written to stderr. Off by default, so a smoke run's output is the
// host's own verdict lines.
void smoke_host_set_plugin_logging(bool enabled);
