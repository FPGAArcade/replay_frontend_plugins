// The smallest Replay emulator plugin in C, and the reference every plugin in this repository
// follows: one include, one instance struct, one RpEmuAPI, one exported entry point.
//
// There is no emulator behind it. It synthesizes a framebuffer and an audio ramp from a frame
// counter, so a given frame index always produces the same bytes -- enough to prove the whole
// path from ./build.sh through --deploy to the frontend launching it.
//
// Every fl_*/arena_* call binds to the flowi shared library; every rp_* call binds to the host
// process when the plugin is loaded. The plugin links neither.
#include <replay/plugin.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define STUB_WIDTH 320
#define STUB_HEIGHT 240
#define STUB_FPS 50
#define STUB_SAMPLE_RATE 48000
#define STUB_CHANNELS 2
// One video frame's worth of audio, which divides exactly at these two rates.
#define STUB_FRAMES_PER_CALL (STUB_SAMPLE_RATE / STUB_FPS)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct StubEmu {
    u32 pixels[STUB_WIDTH * STUB_HEIGHT];
    f32 samples[STUB_FRAMES_PER_CALL * STUB_CHANNELS];
    // Advanced by run_frame and reset to zero by reset()/hard_reset(); the sole input to both
    // generators, which is what makes a frame index reproduce its bytes.
    u32 frame_index;
    bool media_mounted;
} StubEmu;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_get_info(RpEmuInfo* info) {
    info->emu_name = "Stub";
    info->emu_version = "1.0";
    info->system_name = "Stub System";
    info->supported_extensions = "stub";
    info->requires_bios = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* stub_create(FlArena* arena) {
    StubEmu* emu = arena_alloc_zero(arena, StubEmu);
    fl_log_info("Stub emulator created");
    return emu;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_destroy(void* instance) {
    // The instance is arena memory the host owns; it goes when the arena does.
    (void)instance;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Accepts any path: there is no media to read.

static bool stub_mount_media(void* instance, const char* path) {
    StubEmu* emu = (StubEmu*)instance;
    (void)path;
    emu->media_mounted = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_unmount_media(void* instance) {
    StubEmu* emu = (StubEmu*)instance;
    emu->media_mounted = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_run_frame(void* instance, RpEmuFrameContext* ctx) {
    StubEmu* emu = (StubEmu*)instance;

    // A diagonal gradient scrolling with the frame counter: every channel varies across both axes
    // and over time, so a stuck row, column or frame shows up as a byte difference.
    for (u32 y = 0; y < STUB_HEIGHT; y++) {
        for (u32 x = 0; x < STUB_WIDTH; x++) {
            const u32 red = (x + emu->frame_index) & 0xff;
            const u32 green = (y + emu->frame_index) & 0xff;
            const u32 blue = (x + y + emu->frame_index) & 0xff;
            emu->pixels[(y * STUB_WIDTH) + x] = (red << 16) | (green << 8) | blue;
        }
    }

    // A sawtooth over the frame, offset per channel so a swapped pair is visible.
    for (u32 frame = 0; frame < STUB_FRAMES_PER_CALL; frame++) {
        const f32 phase = (f32)((frame + emu->frame_index) % STUB_FRAMES_PER_CALL);
        const f32 value = (phase / (f32)STUB_FRAMES_PER_CALL) - 0.5f;
        emu->samples[frame * STUB_CHANNELS] = value;
        emu->samples[(frame * STUB_CHANNELS) + 1] = -value;
    }

    emu->frame_index++;

    ctx->video_buffer = emu->pixels;
    ctx->video_pitch = STUB_WIDTH * (u32)sizeof(u32);
    ctx->video_width = STUB_WIDTH;
    ctx->video_height = STUB_HEIGHT;
    ctx->audio_buffer = emu->samples;
    ctx->audio_frames = STUB_FRAMES_PER_CALL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_reset(void* instance) {
    StubEmu* emu = (StubEmu*)instance;
    emu->frame_index = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_hard_reset(void* instance) {
    StubEmu* emu = (StubEmu*)instance;
    emu->frame_index = 0;
    emu->media_mounted = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_get_video_spec(void* instance, RpVideoSpec* spec) {
    (void)instance;
    spec->width = STUB_WIDTH;
    spec->height = STUB_HEIGHT;
    spec->pixel_format = RpPixelFormat_Xrgb8888;
    spec->fps = (f32)STUB_FPS;
    spec->visible_x = 0;
    spec->visible_y = 0;
    spec->visible_width = STUB_WIDTH;
    spec->visible_height = STUB_HEIGHT;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_get_audio_spec(void* instance, RpAudioSpec* spec) {
    (void)instance;
    spec->sample_rate = STUB_SAMPLE_RATE;
    spec->channels = STUB_CHANNELS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RpEmuLifecycleState stub_get_state(void* instance) {
    StubEmu* emu = (StubEmu*)instance;
    return emu->media_mounted ? RpEmuLifecycleState_Running : RpEmuLifecycleState_Idle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RpEmuAPI s_stub_api = {
    .get_info = stub_get_info,
    .create = stub_create,
    .destroy = stub_destroy,
    .mount_media = stub_mount_media,
    .unmount_media = stub_unmount_media,
    .run_frame = stub_run_frame,
    .reset = stub_reset,
    .hard_reset = stub_hard_reset,
    .get_video_spec = stub_get_video_spec,
    .get_audio_spec = stub_get_audio_spec,
    .get_state = stub_get_state,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void);

RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void) {
    return &s_stub_api;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ABI version export. The host resolves this before it reads the vtable above and refuses the
// plugin when the answer is not the version it was built for.

RP_EMU_PLUGIN_ABI_VERSION_EXPORT()

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
