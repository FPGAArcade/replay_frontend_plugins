#include "host_symbols.h"

#include <flowi/arena/arena.h>
#include <flowi/core/log.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Enough for the instance state of a real emulator core; a smoke run is short and the arena never
// frees, so the whole run fits in one reservation.
#define SMOKE_ARENA_BYTES (256u * 1024u * 1024u)

// FlArena is opaque to a plugin, so the host owning it defines its own shape. This one is a bump
// allocator over a single block: a smoke run ends by exiting, and nothing needs releasing before
// then.
struct FlArena {
    u8* base;
    u64 size;
    u64 pos;
};

static struct FlArena s_arena;
static bool s_plugin_logging = false;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlArena* smoke_host_arena(void) {
    if (!s_arena.base) {
        s_arena.base = (u8*)malloc(SMOKE_ARENA_BYTES);
        if (!s_arena.base) {
            fprintf(stderr, "smoke: out of memory reserving the plugin arena\n");
            exit(1);
        }
        s_arena.size = SMOKE_ARENA_BYTES;
    }
    return &s_arena;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void smoke_host_set_plugin_logging(bool enabled) {
    s_plugin_logging = enabled;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* arena_bump(FlArena* arena, u64 size, u64 alignment) {
    if (!arena || alignment == 0) {
        return nullptr;
    }
    const u64 aligned = (arena->pos + (alignment - 1)) & ~(alignment - 1);
    if (aligned + size > arena->size) {
        fprintf(stderr, "smoke: the plugin asked for %llu bytes and the arena is full\n", (unsigned long long)size);
        exit(1);
    }
    arena->pos = aligned + size;
    return arena->base + aligned;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The exported symbols themselves. Their prototypes come from the SDK headers above, so a
// signature that drifts from the ABI fails to compile here.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* arena_alloc_raw(FlArena* arena, u64 size, u64 alignment) {
    return arena_bump(arena, size, alignment);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* arena_alloc_raw_zero(FlArena* arena, u64 size, u64 alignment) {
    void* memory = arena_bump(arena, size, alignment);
    if (memory) {
        memset(memory, 0, size);
    }
    return memory;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_log_message(FlLogLevel level, const char* file, i32 line, const char* format, ...) {
    if (!s_plugin_logging) {
        return;
    }
    fprintf(stderr, "smoke: plugin log (level %d, %s:%d): ", (int)level, file, line);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}
