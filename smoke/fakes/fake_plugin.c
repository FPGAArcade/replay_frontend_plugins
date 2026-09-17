///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The negative half of the smoke host's own test: plugins built to fail, one way each.
//
// One source, three shared objects, chosen by the FAKE_* define its target sets. They exist so
// that "the stub passes" is not the only thing the host is known to do -- a smoke host that never
// fails anything proves nothing about the plugins it passes.
//
//   FAKE_HANG    run_frame never returns            -> the watchdog ends the run
//   FAKE_BLANK   every pixel carries the same value -> the blank-framebuffer assertion
//   FAKE_ABI     reports an ABI version that is not this one
//   FAKE_NOABI   exports no ABI version at all
//   FAKE_NOSLOT  leaves a required vtable slot empty
//   FAKE_SILENT  runs correctly but emits no audio, so it passes a smoke that does not assert
//                audio and fails one that does
//   FAKE_NOMEDIA accepts only a null media path, the way a machine that boots from its own ROM
//                is asked to come up with nothing mounted
//   FAKE_STRINGS round-trips a string through the host's string_copy/string_equals and refuses to
//                mount unless it survives
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include <replay/plugin.h>

#if !defined(FAKE_HANG) && !defined(FAKE_BLANK) && !defined(FAKE_ABI) && !defined(FAKE_NOABI) && \
    !defined(FAKE_NOSLOT) && !defined(FAKE_SILENT) && !defined(FAKE_NOMEDIA) && !defined(FAKE_STRINGS)
#error "define one of FAKE_HANG, FAKE_BLANK, FAKE_ABI, FAKE_NOABI, FAKE_NOSLOT, FAKE_SILENT, FAKE_NOMEDIA or FAKE_STRINGS"
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define FAKE_WIDTH 64
#define FAKE_HEIGHT 32
#define FAKE_FPS 50
#define FAKE_SAMPLE_RATE 48000
#define FAKE_CHANNELS 2

typedef struct FakeEmu {
    u32 pixels[FAKE_WIDTH * FAKE_HEIGHT];
    u32 frame_index;
    bool strings_ok;
} FakeEmu;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void fake_get_info(RpEmuInfo* info) {
    info->emu_name = "Fake";
    info->emu_version = "1.0";
    info->system_name = "Smoke host self-test";
    info->supported_extensions = "bin";
    info->requires_bios = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* fake_create(FlArena* arena) {
    FakeEmu* emu = arena_alloc_zero(arena, FakeEmu);
    // A real plugin reaches the host's string functions, so the host has to define them; a plugin
    // that finds one missing fails at load, and one that finds a broken one fails far later.
    const FlString original = string_from_cstr("floppy_a.st");
    const FlString copied = string_copy(arena, original);
    // Same length as the original, one byte apart, so a string_equals that only compares lengths
    // fails here rather than passing on the round-trip alone.
    const FlString near_miss = string_from_cstr("floppy_b.st");
    emu->strings_ok = copied.data != original.data && string_equals(copied, original) &&
                      !string_equals(copied, near_miss);
    return emu;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void fake_destroy(void* instance) {
    (void)instance;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool fake_mount_media(void* instance, const char* path) {
#if defined(FAKE_STRINGS)
    (void)path;
    return ((FakeEmu*)instance)->strings_ok;
#elif defined(FAKE_NOMEDIA)
    (void)instance;
    // Accepts only a null path, which is what proves the host asks for a boot with nothing
    // mounted when smoke.toml names no fixture, rather than handing over an empty string.
    return path == nullptr;
#else
    (void)instance;
    (void)path;
    return true;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void fake_unmount_media(void* instance) {
    (void)instance;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if !defined(FAKE_NOSLOT)

#if defined(FAKE_HANG)
// Volatile so the compiler cannot treat the loop as unreachable and optimise it away.
static volatile bool s_spin = true;
#endif

static void fake_run_frame(void* instance, RpEmuFrameContext* ctx) {
    FakeEmu* emu = (FakeEmu*)instance;

#if defined(FAKE_HANG)
    while (s_spin) {}
#endif

    for (u32 i = 0; i < FAKE_WIDTH * FAKE_HEIGHT; i++) {
#if defined(FAKE_BLANK)
        emu->pixels[i] = 0;
#else
        emu->pixels[i] = (i + emu->frame_index) & 0xffffff;
#endif
    }
    emu->frame_index++;

    ctx->video_buffer = emu->pixels;
    ctx->video_pitch = FAKE_WIDTH * (u32)sizeof(u32);
    ctx->video_width = FAKE_WIDTH;
    ctx->video_height = FAKE_HEIGHT;
}

#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void fake_reset(void* instance) {
    FakeEmu* emu = (FakeEmu*)instance;
    emu->frame_index = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void fake_hard_reset(void* instance) {
    fake_reset(instance);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void fake_get_video_spec(void* instance, RpVideoSpec* spec) {
    (void)instance;
    spec->width = FAKE_WIDTH;
    spec->height = FAKE_HEIGHT;
    spec->pixel_format = RpPixelFormat_Xrgb8888;
    spec->fps = (f32)FAKE_FPS;
    spec->visible_width = FAKE_WIDTH;
    spec->visible_height = FAKE_HEIGHT;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void fake_get_audio_spec(void* instance, RpAudioSpec* spec) {
    (void)instance;
    spec->sample_rate = FAKE_SAMPLE_RATE;
    spec->channels = FAKE_CHANNELS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RpEmuLifecycleState fake_get_state(void* instance) {
    (void)instance;
    return RpEmuLifecycleState_Running;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RpEmuAPI s_fake_api = {
#if !defined(FAKE_NOSLOT)
    .run_frame = fake_run_frame,
#endif
    .get_info = fake_get_info,
    .create = fake_create,
    .destroy = fake_destroy,
    .mount_media = fake_mount_media,
    .unmount_media = fake_unmount_media,
    .reset = fake_reset,
    .hard_reset = fake_hard_reset,
    .get_video_spec = fake_get_video_spec,
    .get_audio_spec = fake_get_audio_spec,
    .get_state = fake_get_state,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void);

RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void) {
    return &s_fake_api;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// FAKE_NOABI leaves the export out altogether and FAKE_ABI answers a version this host does not
// speak; every other fake carries the honest one, which is what a real plugin's
// RP_EMU_PLUGIN_ABI_VERSION_EXPORT() expands to.
#if !defined(FAKE_NOABI)

RP_EMU_EXPORT u64 rp_emu_plugin_abi_version(void);

RP_EMU_EXPORT u64 rp_emu_plugin_abi_version(void) {
#if defined(FAKE_ABI)
    return RP_PLUGIN_ABI_VERSION + 1;
#else
    return RP_PLUGIN_ABI_VERSION;
#endif
}

#endif
