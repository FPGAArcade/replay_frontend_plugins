#include "smoke_config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A fixture's provenance record is this suffix appended to the fixture's own name, so provenance
// is per file and cannot drift onto the wrong one.
#define PROVENANCE_SUFFIX ".provenance.toml"

// A run long enough to be a hang, or short enough to prove nothing, is a configuration mistake.
#define SMOKE_MAX_FRAMES 100000u
#define SMOKE_MAX_TIMEOUT_SECONDS 3600u

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The configuration format: one `key = value` per line, values being "strings", unsigned integers
// and true/false. That is the subset of TOML both smoke.toml and a provenance record use, and it
// is small enough to read here -- the smoke host implements the flowi symbols a plugin binds to,
// so it cannot call flowi's own parsers without becoming a second flowi.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum TomlType {
    TomlType_String,
    TomlType_U32,
    TomlType_Bool,
} TomlType;

typedef struct TomlField {
    const char* name;
    TomlType type;
    void* value;
    // Bytes available at `value`, for TomlType_String only.
    u32 value_size;
    bool required;
    bool seen;
} TomlField;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static char* trim(char* text) {
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }
    char* end = text + strlen(text);
    while (end > text) {
        const char previous = end[-1];
        if (previous != ' ' && previous != '\t' && previous != '\r' && previous != '\n') {
            break;
        }
        end--;
    }
    *end = '\0';
    return text;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Truncates at the first `#` that is not inside a quoted string, so a URL fragment in a provenance
// source survives.
static void strip_comment(char* line) {
    bool in_string = false;
    for (char* cursor = line; *cursor; cursor++) {
        if (*cursor == '"') {
            in_string = !in_string;
        } else if (*cursor == '#' && !in_string) {
            *cursor = '\0';
            return;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool parse_value(const char* path, u32 line_number, TomlField* field, char* text) {
    switch (field->type) {
        case TomlType_String: {
            const size_t length = strlen(text);
            if (length < 2 || text[0] != '"' || text[length - 1] != '"') {
                fprintf(stderr, "smoke: %s:%u: '%s' must be a quoted string\n", path, line_number, field->name);
                return false;
            }
            text[length - 1] = '\0';
            const char* body = text + 1;
            if (strlen(body) == 0) {
                fprintf(stderr, "smoke: %s:%u: '%s' must not be empty\n", path, line_number, field->name);
                return false;
            }
            if (strlen(body) + 1 > field->value_size) {
                fprintf(stderr, "smoke: %s:%u: '%s' is longer than %u bytes\n", path, line_number, field->name,
                        field->value_size - 1);
                return false;
            }
            memcpy(field->value, body, strlen(body) + 1);
            return true;
        }
        case TomlType_U32: {
            char* end = nullptr;
            errno = 0;
            const unsigned long parsed = strtoul(text, &end, 10);
            if (errno != 0 || end == text || *end != '\0' || parsed > 0xffffffffUL) {
                fprintf(stderr, "smoke: %s:%u: '%s' must be an unsigned integer\n", path, line_number, field->name);
                return false;
            }
            *(u32*)field->value = (u32)parsed;
            return true;
        }
        case TomlType_Bool: {
            if (strcmp(text, "true") == 0) {
                *(bool*)field->value = true;
                return true;
            }
            if (strcmp(text, "false") == 0) {
                *(bool*)field->value = false;
                return true;
            }
            fprintf(stderr, "smoke: %s:%u: '%s' must be true or false\n", path, line_number, field->name);
            return false;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool toml_read(const char* path, TomlField* fields, u32 field_count) {
    FILE* file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "smoke: cannot read %s: %s\n", path, strerror(errno));
        return false;
    }

    bool ok = true;
    char line[SMOKE_PATH_MAX + 256];
    u32 line_number = 0;

    while (fgets(line, (int)sizeof(line), file)) {
        line_number++;
        strip_comment(line);
        char* content = trim(line);
        if (*content == '\0') {
            continue;
        }

        char* separator = strchr(content, '=');
        if (!separator) {
            fprintf(stderr, "smoke: %s:%u: expected 'key = value'\n", path, line_number);
            ok = false;
            continue;
        }
        *separator = '\0';
        const char* key = trim(content);
        char* value = trim(separator + 1);

        TomlField* field = nullptr;
        for (u32 i = 0; i < field_count; i++) {
            if (strcmp(fields[i].name, key) == 0) {
                field = &fields[i];
                break;
            }
        }
        if (!field) {
            fprintf(stderr, "smoke: %s:%u: unknown key '%s'\n", path, line_number, key);
            ok = false;
            continue;
        }
        if (field->seen) {
            fprintf(stderr, "smoke: %s:%u: '%s' set twice\n", path, line_number, key);
            ok = false;
            continue;
        }
        field->seen = true;
        ok = parse_value(path, line_number, field, value) && ok;
    }

    fclose(file);

    for (u32 i = 0; i < field_count; i++) {
        if (fields[i].required && !fields[i].seen) {
            fprintf(stderr, "smoke: %s: '%s' is required\n", path, fields[i].name);
            ok = false;
        }
    }
    return ok;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Resolves `relative` against the directory `base_file` lives in, so a smoke.toml names its
// fixture the way a reader expects: relative to itself.
static bool resolve_beside(const char* base_file, const char* relative, char* out, u32 out_size) {
    if (relative[0] == '/') {
        if (strlen(relative) + 1 > out_size) {
            fprintf(stderr, "smoke: path is longer than %u bytes: %s\n", out_size - 1, relative);
            return false;
        }
        memcpy(out, relative, strlen(relative) + 1);
        return true;
    }

    const char* last_slash = strrchr(base_file, '/');
    const size_t directory_length = last_slash ? (size_t)(last_slash - base_file) + 1 : 0;
    if (directory_length + strlen(relative) + 1 > out_size) {
        fprintf(stderr, "smoke: path is longer than %u bytes: %s%s\n", out_size - 1, base_file, relative);
        return false;
    }
    memcpy(out, base_file, directory_length);
    memcpy(out + directory_length, relative, strlen(relative) + 1);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool smoke_config_read(const char* path, SmokeConfig* out_config) {
    *out_config = (SmokeConfig) { 0 };

    char fixture[SMOKE_PATH_MAX] = { 0 };
    TomlField fields[] = {
        { "fixture", TomlType_String, fixture, (u32)sizeof(fixture), true, false },
        { "frames", TomlType_U32, &out_config->frames, 0, true, false },
        { "timeout_seconds", TomlType_U32, &out_config->timeout_seconds, 0, true, false },
        { "audio", TomlType_Bool, &out_config->audio, 0, false, false },
    };

    if (!toml_read(path, fields, (u32)(sizeof(fields) / sizeof(fields[0])))) {
        return false;
    }

    if (out_config->frames == 0 || out_config->frames > SMOKE_MAX_FRAMES) {
        fprintf(stderr, "smoke: %s: 'frames' must be between 1 and %u\n", path, SMOKE_MAX_FRAMES);
        return false;
    }
    if (out_config->timeout_seconds == 0 || out_config->timeout_seconds > SMOKE_MAX_TIMEOUT_SECONDS) {
        fprintf(stderr, "smoke: %s: 'timeout_seconds' must be between 1 and %u\n", path, SMOKE_MAX_TIMEOUT_SECONDS);
        return false;
    }

    return resolve_beside(path, fixture, out_config->fixture, (u32)sizeof(out_config->fixture));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool smoke_fixture_provenance_ok(const char* fixture_path) {
    FILE* fixture = fopen(fixture_path, "rb");
    if (!fixture) {
        fprintf(stderr, "smoke: no fixture at %s: %s\n", fixture_path, strerror(errno));
        return false;
    }
    fclose(fixture);

    char provenance_path[SMOKE_PATH_MAX];
    if (strlen(fixture_path) + strlen(PROVENANCE_SUFFIX) + 1 > sizeof(provenance_path)) {
        fprintf(stderr, "smoke: fixture path is too long to append " PROVENANCE_SUFFIX ": %s\n", fixture_path);
        return false;
    }
    snprintf(provenance_path, sizeof(provenance_path), "%s%s", fixture_path, PROVENANCE_SUFFIX);

    char source[512] = { 0 };
    char license[512] = { 0 };
    TomlField fields[] = {
        { "source", TomlType_String, source, (u32)sizeof(source), true, false },
        { "license", TomlType_String, license, (u32)sizeof(license), true, false },
    };

    if (!toml_read(provenance_path, fields, (u32)(sizeof(fields) / sizeof(fields[0])))) {
        fprintf(stderr, "smoke: %s has no usable provenance record at %s\n", fixture_path, provenance_path);
        return false;
    }

    printf("smoke: fixture %s\n", fixture_path);
    printf("smoke:   source  %s\n", source);
    printf("smoke:   license %s\n", license);
    return true;
}
