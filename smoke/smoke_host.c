///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The Replay plugin smoke host.
//
//   replay_smoke <plugin.so> <smoke.toml>
//
// "It loads" is not "it runs". This loads a built emulator plugin the way the frontend does,
// mounts the fixture its smoke.toml names, runs the frames it asks for, and then asserts the two
// things a published plugin must do: still be running, and have produced a framebuffer that is not
// blank. Audio is asserted only when the plugin opts in.
//
// It links nothing but the plugin SDK's headers. The fl_*/arena_* symbols a plugin binds to come
// from host_symbols.c in this executable, which is why adding a plugin's smoke is writing one
// smoke.toml and no C at all.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "host_symbols.h"
#include "smoke_config.h"

#include <dlfcn.h>
#include <replay/emu_plugin.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Exit statuses. Anything non-zero is a failed smoke; the split tells a caller whether the plugin
// failed or the invocation was wrong.
#define SMOKE_EXIT_PASS 0
#define SMOKE_EXIT_FAIL 1
#define SMOKE_EXIT_USAGE 2

// An optional export: a plugin that carries it must have been built against this ABI. Most do not,
// and the entry point below is then the whole contract.
#define SMOKE_ABI_VERSION_SYMBOL "rp_emu_plugin_abi_version"
#define SMOKE_ENTRY_POINT_SYMBOL "rp_emu_plugin_get"

typedef u32 (*RpEmuPluginAbiVersionFunc)(void);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The vtable slots <replay/plugin/emu_plugin_types.h> does not document as optional. A plugin
// missing one of these is not one the frontend could drive.
typedef struct RequiredSlot {
    const char* name;
    size_t offset;
} RequiredSlot;

#define REQUIRED_SLOT(field) { #field, offsetof(RpEmuAPI, field) }

static const RequiredSlot s_required_slots[] = {
    REQUIRED_SLOT(get_info),       REQUIRED_SLOT(create),         REQUIRED_SLOT(destroy),   REQUIRED_SLOT(mount_media),
    REQUIRED_SLOT(unmount_media),  REQUIRED_SLOT(run_frame),      REQUIRED_SLOT(reset),     REQUIRED_SLOT(hard_reset),
    REQUIRED_SLOT(get_video_spec), REQUIRED_SLOT(get_audio_spec), REQUIRED_SLOT(get_state),
};

#undef REQUIRED_SLOT

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The watchdog. A plugin that never returns from run_frame would otherwise hang the run forever,
// so SIGALRM ends the process instead. Only async-signal-safe calls belong in here.
static void on_timeout(int signal_number) {
    (void)signal_number;
    static const char message[] = "smoke: FAIL - the plugin did not finish its frames before the timeout\n";
    ssize_t ignored = write(STDERR_FILENO, message, sizeof(message) - 1);
    (void)ignored;
    _exit(SMOKE_EXIT_FAIL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u32 bytes_per_pixel(RpPixelFormat format) {
    switch (format) {
        case RpPixelFormat_Xrgb8888:
            return 4;
        case RpPixelFormat_Rgb565:
            return 2;
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A frame's dimensions as the host reads them: the context overrides the video spec per frame, and
// a zero pitch means the rows are packed.
typedef struct FrameGeometry {
    u32 width;
    u32 height;
    u32 pixel_size;
    u32 pitch;
} FrameGeometry;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FrameGeometry frame_geometry(const RpEmuFrameContext* ctx, const RpVideoSpec* spec) {
    FrameGeometry geometry = { 0 };
    geometry.width = ctx->video_width ? ctx->video_width : spec->width;
    geometry.height = ctx->video_height ? ctx->video_height : spec->height;
    geometry.pixel_size = bytes_per_pixel(spec->pixel_format);
    geometry.pitch = ctx->video_pitch ? ctx->video_pitch : geometry.width * geometry.pixel_size;
    return geometry;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Rejects a frame the host could not read at all, separately from one it read and found blank.
static bool frame_is_readable(const RpEmuFrameContext* ctx, const RpVideoSpec* spec, const FrameGeometry* geometry,
                              u32 frame_index) {
    if (geometry->pixel_size == 0) {
        fprintf(stderr, "smoke: FAIL - frame %u: unknown pixel format %d\n", frame_index, (int)spec->pixel_format);
        return false;
    }
    if (!ctx->video_buffer) {
        fprintf(stderr, "smoke: FAIL - frame %u: the plugin produced no framebuffer\n", frame_index);
        return false;
    }
    if (geometry->width == 0 || geometry->height == 0) {
        fprintf(stderr, "smoke: FAIL - frame %u: framebuffer is %ux%u\n", frame_index, geometry->width,
                geometry->height);
        return false;
    }
    if (geometry->pitch < geometry->width * geometry->pixel_size) {
        fprintf(stderr, "smoke: FAIL - frame %u: pitch %u is short for %u pixels of %u bytes\n", frame_index,
                geometry->pitch, geometry->width, geometry->pixel_size);
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Blank means every visible pixel carries the same value, which covers an all-black frame and an
// all-anything frame alike. A core that is still on its boot screen looks blank here, so a
// plugin's `frames` has to run past it.
static bool framebuffer_is_blank(const RpEmuFrameContext* ctx, const FrameGeometry* geometry) {
    const u8* base = (const u8*)ctx->video_buffer;

    for (u32 y = 0; y < geometry->height; y++) {
        const u8* row = base + ((size_t)y * geometry->pitch);
        for (u32 x = 0; x < geometry->width; x++) {
            if (memcmp(row + ((size_t)x * geometry->pixel_size), base, geometry->pixel_size) != 0) {
                return false;
            }
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool audio_has_signal(const RpEmuFrameContext* ctx, const RpAudioSpec* spec) {
    if (!ctx->audio_buffer || ctx->audio_frames == 0) {
        return false;
    }
    const u32 samples = ctx->audio_frames * spec->channels;
    for (u32 i = 0; i < samples; i++) {
        if (ctx->audio_buffer[i] != 0.0f) {
            return true;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const RpEmuAPI* load_plugin(const char* path) {
    // RTLD_NOW, not lazy: a call this host does not implement then fails here, with the symbol
    // named, instead of crashing in the middle of a frame.
    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "smoke: FAIL - cannot load %s: %s\n", path, dlerror());
        return nullptr;
    }

    // dlsym returns a data pointer; converting it to a function pointer is what dlopen is for, and
    // the cast through a union avoids the ISO C object/function pointer complaint.
    union {
        void* object;
        RpEmuPluginAbiVersionFunc abi_version;
        RpEmuPluginGetFunc entry_point;
    } symbol;

    symbol.object = dlsym(handle, SMOKE_ABI_VERSION_SYMBOL);
    if (symbol.object) {
        const u32 reported = symbol.abi_version();
        if (reported != RP_EMU_API_VERSION) {
            fprintf(stderr, "smoke: FAIL - %s reports ABI version %u; this host speaks %u\n", path, reported,
                    (u32)RP_EMU_API_VERSION);
            return nullptr;
        }
    }

    symbol.object = dlsym(handle, SMOKE_ENTRY_POINT_SYMBOL);
    if (!symbol.object) {
        fprintf(stderr, "smoke: FAIL - %s exports no " SMOKE_ENTRY_POINT_SYMBOL "\n", path);
        return nullptr;
    }

    const RpEmuAPI* api = symbol.entry_point();
    if (!api) {
        fprintf(stderr, "smoke: FAIL - " SMOKE_ENTRY_POINT_SYMBOL " returned nothing\n");
        return nullptr;
    }

    bool complete = true;
    for (size_t i = 0; i < sizeof(s_required_slots) / sizeof(s_required_slots[0]); i++) {
        const void* slot = *(void* const*)((const u8*)api + s_required_slots[i].offset);
        if (!slot) {
            fprintf(stderr, "smoke: FAIL - %s leaves the required slot '%s' empty\n", path, s_required_slots[i].name);
            complete = false;
        }
    }
    return complete ? api : nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool run_frames(const RpEmuAPI* api, void* instance, const SmokeConfig* config) {
    RpVideoSpec video = { 0 };
    RpAudioSpec audio = { 0 };
    api->get_video_spec(instance, &video);
    api->get_audio_spec(instance, &audio);

    if (config->audio && audio.channels == 0) {
        fprintf(stderr, "smoke: FAIL - this plugin's smoke asserts audio and it reports zero channels\n");
        return false;
    }

    const RpEmuInputState input = { 0 };
    const double delta_time = video.fps > 0.0f ? 1.0 / (double)video.fps : 0.0;
    bool blank_final_frame = true;
    bool heard_audio = false;

    for (u32 frame = 0; frame < config->frames; frame++) {
        RpEmuFrameContext ctx = { 0 };
        ctx.input = &input;
        ctx.delta_time = delta_time;
        api->run_frame(instance, &ctx);

        const FrameGeometry geometry = frame_geometry(&ctx, &video);
        if (!frame_is_readable(&ctx, &video, &geometry, frame)) {
            return false;
        }
        blank_final_frame = framebuffer_is_blank(&ctx, &geometry);
        heard_audio = heard_audio || audio_has_signal(&ctx, &audio);
    }

    printf("smoke: ran %u frames at %ux%u\n", config->frames, video.width, video.height);

    if (api->get_state(instance) == RpEmuLifecycleState_Error) {
        fprintf(stderr, "smoke: FAIL - the plugin is in its error state after %u frames\n", config->frames);
        return false;
    }
    if (blank_final_frame) {
        fprintf(stderr, "smoke: FAIL - frame %u is blank: every pixel carries the same value\n", config->frames - 1);
        return false;
    }
    if (config->audio && !heard_audio) {
        fprintf(stderr, "smoke: FAIL - no audio in %u frames, and this plugin's smoke asserts audio\n", config->frames);
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool run_smoke(const RpEmuAPI* api, const SmokeConfig* config) {
    RpEmuInfo info = { 0 };
    api->get_info(&info);
    printf("smoke: %s %s (%s)\n", info.emu_name ? info.emu_name : "?", info.emu_version ? info.emu_version : "?",
           info.system_name ? info.system_name : "?");

    void* instance = api->create(smoke_host_arena());
    if (!instance) {
        fprintf(stderr, "smoke: FAIL - create() returned nothing\n");
        return false;
    }

    // No fixture in smoke.toml means a null path, which is how a plugin is told to boot with
    // nothing mounted -- a machine whose own ROM comes up to a usable screen with an empty drive.
    const char* fixture = config->fixture[0] ? config->fixture : nullptr;

    bool ok = api->mount_media(instance, fixture);
    if (!ok) {
        fprintf(stderr, "smoke: FAIL - the plugin refused %s\n", fixture ? fixture : "booting with no fixture");
    } else {
        ok = run_frames(api, instance, config);
        api->unmount_media(instance);
    }

    api->destroy(instance);
    return ok;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void usage(void) {
    fprintf(stderr, "usage: replay_smoke <plugin.so> <smoke.toml> [--plugin-log]\n"
                    "\n"
                    "Loads a built emulator plugin, mounts the fixture smoke.toml names (or boots with\n"
                    "none when it names no fixture), runs its frames,\n"
                    "and fails unless the plugin is still running with a framebuffer that is not blank.\n"
                    "  --plugin-log   also print what the plugin logs\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv) {
    const char* plugin_path = nullptr;
    const char* config_path = nullptr;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--plugin-log") == 0) {
            smoke_host_set_plugin_logging(true);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return SMOKE_EXIT_PASS;
        } else if (!plugin_path) {
            plugin_path = argv[i];
        } else if (!config_path) {
            config_path = argv[i];
        } else {
            fprintf(stderr, "replay_smoke: unexpected argument '%s'\n", argv[i]);
            usage();
            return SMOKE_EXIT_USAGE;
        }
    }

    if (!plugin_path || !config_path) {
        usage();
        return SMOKE_EXIT_USAGE;
    }

    SmokeConfig config = { 0 };
    if (!smoke_config_read(config_path, &config)) {
        return SMOKE_EXIT_USAGE;
    }
    if (config.fixture[0] && !smoke_fixture_provenance_ok(config.fixture)) {
        return SMOKE_EXIT_FAIL;
    }

    const RpEmuAPI* api = load_plugin(plugin_path);
    if (!api) {
        return SMOKE_EXIT_FAIL;
    }

    signal(SIGALRM, on_timeout);
    alarm(config.timeout_seconds);
    const bool ok = run_smoke(api, &config);
    alarm(0);

    printf("smoke: %s - %s\n", ok ? "PASS" : "FAIL", plugin_path);
    return ok ? SMOKE_EXIT_PASS : SMOKE_EXIT_FAIL;
}
