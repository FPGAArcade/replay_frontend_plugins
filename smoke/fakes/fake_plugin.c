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
//   FAKE_NOSLOT  leaves a required vtable slot empty
//   FAKE_SILENT  runs correctly but emits no audio, so it passes a smoke that does not assert
//                audio and fails one that does
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include <replay/plugin.h>

#if !defined(FAKE_HANG) && !defined(FAKE_BLANK) && !defined(FAKE_ABI) && !defined(FAKE_NOSLOT) && !defined(FAKE_SILENT)
#error "define one of FAKE_HANG, FAKE_BLANK, FAKE_ABI, FAKE_NOSLOT or FAKE_SILENT"
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
    return arena_alloc_zero(arena, FakeEmu);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void fake_destroy(void* instance) {
    (void)instance;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool fake_mount_media(void* instance, const char* path) {
    (void)instance;
    (void)path;
    return true;
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

#if defined(FAKE_ABI)

RP_EMU_EXPORT u32 rp_emu_plugin_abi_version(void);

RP_EMU_EXPORT u32 rp_emu_plugin_abi_version(void) {
    return RP_EMU_API_VERSION + 1;
}

#endif
