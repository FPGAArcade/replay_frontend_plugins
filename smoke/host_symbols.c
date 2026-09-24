#include "host_symbols.h"

#include <flowi/arena/arena.h>
#include <flowi/core/log.h>
#include <flowi/string/string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
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

FlString string_copy(FlArena* arena, FlString str) {
    if (string_is_empty(str)) {
        return string_empty();
    }
    // Null-terminated as well as length-carrying: a plugin that hands the result to a C library
    // expects the terminator, and one byte per copy is nothing against a smoke run's arena.
    char* copy = (char*)arena_bump(arena, str.length + 1, 1);
    memcpy(copy, str.data, str.length);
    copy[str.length] = '\0';
    return string_from_cstr_len(copy, str.length);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool string_equals(FlString a, FlString b) {
    return a.length == b.length && (a.length == 0 || memcmp(a.data, b.data, a.length) == 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A directive is at most flags, two expanded '*' values, a length and a conversion.
#define LOG_SPEC_MAX 64

typedef enum LogLength {
    LogLength_None,
    LogLength_Hh,
    LogLength_H,
    LogLength_L,
    LogLength_Ll,
    LogLength_Z,
    LogLength_J,
    LogLength_T,
    LogLength_BigL,
    LogLength_Unknown,
} LogLength;

static LogLength log_length(const char* text, size_t len) {
    static const char* const names[] = { "", "hh", "h", "l", "ll", "z", "j", "t", "L" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strlen(names[i]) == len && strncmp(names[i], text, len) == 0) {
            return (LogLength)i;
        }
    }
    return LogLength_Unknown;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Prints one conversion, pulling its argument by type. False when this walker does not know it.
static bool log_conversion(FILE* out, const char* spec, char conversion, LogLength length, va_list* args) {
    switch (conversion) {
        case 'S': {
            const FlString str = va_arg(*args, FlString);
            if (str.data) {
                fwrite(str.data, 1, str.length, out);
            } else {
                fputs("null", out);
            }
            return true;
        }
        case 's': {
            if (length != LogLength_None) {
                return false;
            }
            const char* str = va_arg(*args, const char*);
            fprintf(out, spec, str ? str : "null");
            return true;
        }
        case 'c':
            if (length != LogLength_None) {
                return false;
            }
            fprintf(out, spec, va_arg(*args, int));
            return true;
        case 'p':
            if (length != LogLength_None) {
                return false;
            }
            fprintf(out, spec, va_arg(*args, void*));
            return true;
        case 'd':
        case 'i':
            switch (length) {
                case LogLength_None:
                case LogLength_Hh:
                case LogLength_H:
                    fprintf(out, spec, va_arg(*args, int));
                    return true;
                case LogLength_L:
                    fprintf(out, spec, va_arg(*args, long));
                    return true;
                case LogLength_Ll:
                    fprintf(out, spec, va_arg(*args, long long));
                    return true;
                case LogLength_Z:
                case LogLength_T:
                    fprintf(out, spec, va_arg(*args, ptrdiff_t));
                    return true;
                case LogLength_J:
                    fprintf(out, spec, va_arg(*args, intmax_t));
                    return true;
                default:
                    return false;
            }
        case 'u':
        case 'o':
        case 'x':
        case 'X':
            switch (length) {
                case LogLength_None:
                case LogLength_Hh:
                case LogLength_H:
                    fprintf(out, spec, va_arg(*args, unsigned));
                    return true;
                case LogLength_L:
                    fprintf(out, spec, va_arg(*args, unsigned long));
                    return true;
                case LogLength_Ll:
                    fprintf(out, spec, va_arg(*args, unsigned long long));
                    return true;
                case LogLength_Z:
                case LogLength_T:
                    fprintf(out, spec, va_arg(*args, size_t));
                    return true;
                case LogLength_J:
                    fprintf(out, spec, va_arg(*args, uintmax_t));
                    return true;
                default:
                    return false;
            }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            switch (length) {
                case LogLength_None:
                case LogLength_L:
                    fprintf(out, spec, va_arg(*args, double));
                    return true;
                case LogLength_BigL:
                    fprintf(out, spec, va_arg(*args, long double));
                    return true;
                default:
                    return false;
            }
        default:
            return false;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Stops writing once the spec is full; the caller sees the overflow as n >= LOG_SPEC_MAX.
static void spec_appendf(char* spec, int* n, const char* format, ...) {
    if (*n >= LOG_SPEC_MAX) {
        return;
    }
    va_list args;
    va_start(args, format);
    *n += vsnprintf(spec + *n, (size_t)(LOG_SPEC_MAX - *n), format, args);
    va_end(args);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// flowi's formatting for what a plugin logs: %S takes an FlString by value, which vfprintf reads
// as a wide string. A va_list cannot be handed to vfprintf part-consumed, so each directive is
// printed on its own. An unknown one leaves the size of every later argument unknowable, so it is
// flagged and the rest of the format is printed raw.
static void log_format(FILE* out, const char* format, va_list* args) {
    const char* p = format;
    while (*p) {
        if (*p != '%') {
            fputc(*p++, out);
            continue;
        }
        const char* directive = p++;
        if (*p == '%') {
            fputc(*p++, out);
            continue;
        }

        char spec[LOG_SPEC_MAX];
        const size_t flags = strspn(p, "-+ #0");
        int n = 0;
        spec_appendf(spec, &n, "%%%.*s", (int)flags, p);
        p += flags;

        if (*p == '*') {
            spec_appendf(spec, &n, "%d", va_arg(*args, int));
            p++;
        } else {
            const size_t width = strspn(p, "0123456789");
            spec_appendf(spec, &n, "%.*s", (int)width, p);
            p += width;
        }

        if (*p == '.') {
            p++;
            if (*p == '*') {
                // A negative precision reads as none at all.
                const int precision = va_arg(*args, int);
                if (precision >= 0) {
                    spec_appendf(spec, &n, ".%d", precision);
                }
                p++;
            } else {
                const size_t precision = strspn(p, "0123456789");
                spec_appendf(spec, &n, ".%.*s", (int)precision, p);
                p += precision;
            }
        }

        const size_t length_len = strspn(p, "hlzjtL");
        const LogLength length = log_length(p, length_len);
        const char conversion = p[length_len];
        spec_appendf(spec, &n, "%.*s%c", (int)length_len, p, conversion);
        p += length_len;

        if (conversion == '\0' || n >= LOG_SPEC_MAX || !log_conversion(out, spec, conversion, length, args)) {
            fprintf(out, "<unsupported %.*s>%s", (int)(p - directive + (conversion != '\0')), directive,
                    conversion != '\0' ? p + 1 : "");
            return;
        }
        p++;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_log_message(FlLogLevel level, const char* file, i32 line, const char* format, ...) {
    if (!s_plugin_logging) {
        return;
    }
    fprintf(stderr, "smoke: plugin log (level %d, %s:%d): ", (int)level, file, line);
    va_list args;
    va_start(args, format);
    log_format(stderr, format, &args);
    va_end(args);
    fputc('\n', stderr);
}
