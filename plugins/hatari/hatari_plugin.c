///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hatari Core Plugin - Wrapper around hatariB (Atari ST emulator)
//
// This plugin wraps the hatariB libretro core into the Replay Core Plugin API.
// hatariB is a libretro port of Hatari, emulating Atari ST/STE/TT/Falcon computers.
// EmuTOS is bundled as the default free TOS ROM.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <replay/plugin.h>

// Libretro types and constants
#include "libretro.h"

// hatariB core interface
#include "core.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Libretro interface declarations (from hatariB core.c)
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RETRO_API void retro_init(void);
RETRO_API void retro_deinit(void);
RETRO_API void retro_reset(void);
RETRO_API void retro_run(void);
RETRO_API bool retro_load_game(const struct retro_game_info* info);
RETRO_API void retro_unload_game(void);
RETRO_API void retro_get_system_av_info(struct retro_system_av_info* info);
RETRO_API size_t retro_serialize_size(void);
RETRO_API bool retro_serialize(void* data, size_t size);
RETRO_API bool retro_unserialize(const void* data, size_t size);

RETRO_API void retro_set_environment(retro_environment_t cb);
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb);
RETRO_API void retro_set_input_poll(retro_input_poll_t cb);
RETRO_API void retro_set_input_state(retro_input_state_t cb);
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb);
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Video/Audio constants
// TT high resolution can be 1280x960, so we need large buffers
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define VIDEO_MAX_W 2048
#define VIDEO_MAX_H 1024

// Audio buffer: ~4 frames worth at 48kHz/50fps = ~960 samples/frame, stereo
#define AUDIO_BUFFER_MAX_FRAMES 8192

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Global state for libretro callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const RpEmuInputState* g_current_input = nullptr;

// Audio buffer: int16 samples from hatariB, converted to float for Replay
static float g_audio_buffer[AUDIO_BUFFER_MAX_FRAMES * 2];
static size_t g_audio_frame_count = 0;

// Video callback data
static const void* g_video_data = nullptr;
static uint32_t g_video_width = 320;
static uint32_t g_video_height = 200;
static size_t g_video_pitch = 0;

// Converted frame buffer (XRGB8888 -> ABGR8888 for SDL)
static uint32_t g_frame_buffer[VIDEO_MAX_W * VIDEO_MAX_H];

// System/save directories for libretro
static char g_system_directory[4096] = ".";
static char g_save_directory[4096] = ".";

// Variable update flag
static bool g_variables_updated = false;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Libretro callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void RETRO_CALLCONV hatari_log_printf(enum retro_log_level level, const char* fmt, ...) {
    (void)level;
    va_list args;
    va_start(args, fmt);
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Remove trailing newline if present
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }

    fl_log_debug("Hatari: %s", buffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool hatari_environment(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            struct retro_log_callback* cb = (struct retro_log_callback*)data;
            cb->log = hatari_log_printf;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_PERF_INTERFACE:
            return false;

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
            const char** dir = (const char**)data;
            *dir = g_system_directory;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
            const char** dir = (const char**)data;
            *dir = g_save_directory;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY: {
            const char** dir = (const char**)data;
            *dir = g_system_directory;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            const enum retro_pixel_format* fmt = (const enum retro_pixel_format*)data;
            // hatariB always requests XRGB8888
            if (*fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
                fl_log_info("Hatari requested pixel format: XRGB8888");
                return true;
            }
            return false;
        }

        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
            return true;

        case RETRO_ENVIRONMENT_SET_VARIABLES:
            return true;

        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            struct retro_variable* var = (struct retro_variable*)data;
            if (var->key) {
                // Override defaults for clean demo/game experience
                if (strcmp(var->key, "hatarib_show_welcome") == 0) {
                    var->value = "0";
                    return true;
                }
                if (strcmp(var->key, "hatarib_boot_alert") == 0) {
                    var->value = "0";
                    return true;
                }
                if (strcmp(var->key, "hatarib_statusbar") == 0) {
                    var->value = "0";
                    return true;
                }
                if (strcmp(var->key, "hatarib_memory") == 0) {
                    var->value = "4096";
                    return true;
                }
                if (strcmp(var->key, "hatarib_pause_osk") == 0) {
                    var->value = "1"; // No indicator
                    return true;
                }
            }
            var->value = NULL;
            return false;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
            bool* update = (bool*)data;
            *update = g_variables_updated;
            g_variables_updated = false;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            return true;

        case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
            return false;

        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
            return false;

        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
            return true;

        case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION: {
            unsigned* version = (unsigned*)data;
            *version = 1;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_MESSAGE:
        case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
            return true;

        case RETRO_ENVIRONMENT_GET_LANGUAGE: {
            unsigned* lang = (unsigned*)data;
            *lang = RETRO_LANGUAGE_ENGLISH;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_FASTFORWARDING: {
            bool* ff = (bool*)data;
            *ff = false;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_GEOMETRY:
            return true;

        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
            return true;

        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
            return false;

        case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
            return true;

        case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
            return true;

        case RETRO_ENVIRONMENT_GET_MIDI_INTERFACE:
            return false;

        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
            return true;

        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: {
            unsigned* version = (unsigned*)data;
            *version = 2;
            return true;
        }

        default:
            return false;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_video_refresh(const void* data, unsigned width, unsigned height, size_t pitch) {
    if (data == nullptr) {
        return;
    }
    g_video_data = data;
    g_video_width = width;
    g_video_height = height;
    g_video_pitch = pitch;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_input_poll(void) {
    // Nothing to do - we update g_current_input before calling retro_run
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int16_t hatari_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)index;

    if (!g_current_input) {
        return 0;
    }

    // Handle keyboard input (hatariB uses RETRO_DEVICE_KEYBOARD extensively)
    if (device == RETRO_DEVICE_KEYBOARD) {
        // Map libretro keyboard IDs to our keyboard state array
        if (id < 256) {
            return g_current_input->keyboard_keys[id] ? 1 : 0;
        }
        return 0;
    }

    // Handle joypad on port 0 (Atari ST joystick port 1)
    // Only map directions + fire button (A/B). Do NOT pass L/R/Start/Select — hatariB
    // uses those as meta-controls (L1 = on-screen keyboard, Select = pause overlay).
    if (device == RETRO_DEVICE_JOYPAD && port == 0) {
        switch (id) {
            case RETRO_DEVICE_ID_JOYPAD_UP:
                return g_current_input->buttons[RpEmuButton_Up] ? 1 : 0;
            case RETRO_DEVICE_ID_JOYPAD_DOWN:
                return g_current_input->buttons[RpEmuButton_Down] ? 1 : 0;
            case RETRO_DEVICE_ID_JOYPAD_LEFT:
                return g_current_input->buttons[RpEmuButton_Left] ? 1 : 0;
            case RETRO_DEVICE_ID_JOYPAD_RIGHT:
                return g_current_input->buttons[RpEmuButton_Right] ? 1 : 0;
            case RETRO_DEVICE_ID_JOYPAD_A:
            case RETRO_DEVICE_ID_JOYPAD_B:
                return g_current_input->buttons[RpEmuButton_A] ? 1 : 0;
            default:
                return 0;
        }
    }

    // Handle mouse input (Atari ST mouse)
    if (device == RETRO_DEVICE_MOUSE && port == 0) {
        switch (id) {
            case RETRO_DEVICE_ID_MOUSE_X:
                return g_current_input->mouse_x;
            case RETRO_DEVICE_ID_MOUSE_Y:
                return g_current_input->mouse_y;
            case RETRO_DEVICE_ID_MOUSE_LEFT:
                return g_current_input->mouse_left ? 1 : 0;
            case RETRO_DEVICE_ID_MOUSE_RIGHT:
                return g_current_input->mouse_right ? 1 : 0;
            default:
                return 0;
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static size_t hatari_audio_batch(const int16_t* data, size_t frames) {
    if (g_audio_frame_count + frames > AUDIO_BUFFER_MAX_FRAMES) {
        frames = AUDIO_BUFFER_MAX_FRAMES - g_audio_frame_count;
    }

    if (frames == 0) {
        return 0;
    }

    // Convert int16 [-32768, 32767] to float [-1.0, 1.0]
    float* dst = g_audio_buffer + g_audio_frame_count * 2;
    size_t samples = frames * 2;
    for (size_t i = 0; i < samples; i++) {
        dst[i] = (float)data[i] / 32768.0f;
    }

    g_audio_frame_count += frames;
    return frames;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_audio_sample(int16_t left, int16_t right) {
    int16_t samples[2] = { left, right };
    hatari_audio_batch(samples, 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core instance
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct HatariCore {
    FlArena* arena;
    RpEmuLifecycleState state;
    RpVideoSpec video_spec;
    RpAudioSpec audio_spec;
    FlString disk_path;
    bool initialized;
} HatariCore;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_get_info(RpEmuInfo* info) {
    info->emu_name = "hatariB";
    info->emu_version = "0.4";
    info->system_name = "Atari ST";
    info->supported_extensions = "st|msa|dim|stx|ipf|ctr|m3u|m3u8|zip|gz";
    info->requires_bios = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* hatari_create(FlArena* arena) {

    fl_log_info("Creating Hatari core");

    HatariCore* core = arena_alloc_zero(arena, HatariCore);
    core->arena = arena;
    core->state = RpEmuLifecycleState_Idle;
    core->initialized = false;

    // Set up libretro callbacks before init
    // Environment callback MUST be set first - it's called during retro_init
    retro_set_environment(hatari_environment);
    retro_set_video_refresh(hatari_video_refresh);
    retro_set_input_poll(hatari_input_poll);
    retro_set_input_state(hatari_input_state);
    retro_set_audio_sample_batch(hatari_audio_batch);
    retro_set_audio_sample(hatari_audio_sample);

    // Initialize libretro core
    retro_init();
    core->initialized = true;

    // Set up video spec (ST Low default: 320x200 @ 50Hz PAL)
    core->video_spec.width = 320;
    core->video_spec.height = 200;
    core->video_spec.pixel_format = RpPixelFormat_Xrgb8888;
    core->video_spec.fps = 50.0f;
    core->video_spec.visible_x = 0;
    core->video_spec.visible_y = 0;
    core->video_spec.visible_width = 320;
    core->video_spec.visible_height = 200;

    // Set up audio spec (hatariB defaults to 48000 Hz)
    core->audio_spec.sample_rate = 48000;
    core->audio_spec.channels = 2;

    fl_log_info("Hatari core created");
    return core;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_destroy(void* core_instance) {
    if (!core_instance)
        return;

    HatariCore* core = (HatariCore*)core_instance;
    fl_log_info("Destroying Hatari core");

    if (core->initialized) {
        retro_unload_game();
        retro_deinit();
        core->initialized = false;
    }

    g_current_input = nullptr;
    g_audio_frame_count = 0;
    core->state = RpEmuLifecycleState_Idle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A null path boots the machine with no disk in the drive: EmuTOS comes up to its desktop, which
// is all a smoke run needs and is how the Atari behaves with an empty drive.
static bool hatari_mount_media(void* core_instance, const char* path) {
    HatariCore* core = (HatariCore*)core_instance;

    if (!core || !core->initialized) {
        fl_log_error("Invalid core instance");
        return false;
    }

    struct retro_game_info game_info = { 0 };
    game_info.path = path;

    if (path) {
        fl_log_info("Mounting media: %s", path);
    } else {
        fl_log_info("Booting with no media");
    }

    if (!retro_load_game(path ? &game_info : nullptr)) {
        fl_log_error("Failed to boot: %s", path ? path : "no media");
        return false;
    }

    // Get AV info to update specs
    struct retro_system_av_info av_info = { 0 };
    retro_get_system_av_info(&av_info);

    core->video_spec.width = av_info.geometry.base_width;
    core->video_spec.height = av_info.geometry.base_height;
    core->video_spec.visible_width = av_info.geometry.base_width;
    core->video_spec.visible_height = av_info.geometry.base_height;
    core->video_spec.fps = (float)av_info.timing.fps;

    if (av_info.timing.sample_rate > 0) {
        core->audio_spec.sample_rate = (uint32_t)av_info.timing.sample_rate;
    }

    core->disk_path = path ? string_copy(core->arena, string_from_cstr(path)) : (FlString) { 0 };
    core->state = RpEmuLifecycleState_Running;

    fl_log_info("Media mounted: %ux%u @ %.2f fps, audio: %u Hz", core->video_spec.width, core->video_spec.height,
                core->video_spec.fps, core->audio_spec.sample_rate);

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_unmount_media(void* core_instance) {
    HatariCore* core = (HatariCore*)core_instance;

    if (!core || !core->initialized)
        return;

    fl_log_info("Unmounting media");
    retro_unload_game();
    core->state = RpEmuLifecycleState_Idle;
    core->disk_path = (FlString) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_run_frame(void* core_instance, RpEmuFrameContext* ctx) {
    HatariCore* core = (HatariCore*)core_instance;

    if (!core || !core->initialized || core->state != RpEmuLifecycleState_Running) {
        return;
    }

    // Clear audio buffer for this frame
    g_audio_frame_count = 0;

    // Set input state for callbacks
    g_current_input = ctx->input;

    // Clear video data pointer before run
    g_video_data = nullptr;

    // Run one frame
    retro_run();

    // Convert video: XRGB8888 -> ABGR8888 (SDL_PIXELFORMAT_RGBA32 on little-endian)
    unsigned src_width = g_video_width > 0 ? g_video_width : 320;
    unsigned src_height = g_video_height > 0 ? g_video_height : 200;

    if (src_width > VIDEO_MAX_W) {
        src_width = VIDEO_MAX_W;
    }
    if (src_height > VIDEO_MAX_H) {
        src_height = VIDEO_MAX_H;
    }

    if (g_video_data && src_width > 0 && src_height > 0) {
        const uint32_t* src = (const uint32_t*)g_video_data;
        size_t src_pitch_pixels = g_video_pitch > 0 ? (g_video_pitch / 4) : src_width;

        for (unsigned y = 0; y < src_height; y++) {
            const uint32_t* src_row = src + y * src_pitch_pixels;
            uint32_t* dst_row = g_frame_buffer + y * src_width;
            for (unsigned x = 0; x < src_width; x++) {
                uint32_t pixel = src_row[x];
                // Extract RGB from XRGB8888 (0x00RRGGBB)
                uint8_t r = (pixel >> 16) & 0xFF;
                uint8_t g = (pixel >> 8) & 0xFF;
                uint8_t b = pixel & 0xFF;
                // Output as ABGR8888: u32 = 0xAABBGGRR -> memory [R, G, B, A]
                dst_row[x] = (0xFFu << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
            }
        }
    }

    // Update video spec if resolution changed (Atari ST switches modes dynamically)
    if (core->video_spec.width != src_width || core->video_spec.height != src_height) {
        core->video_spec.width = src_width;
        core->video_spec.height = src_height;
        core->video_spec.visible_width = src_width;
        core->video_spec.visible_height = src_height;
        fl_log_info("Resolution changed: %ux%u", src_width, src_height);
    }

    ctx->video_buffer = g_frame_buffer;
    ctx->video_pitch = src_width * sizeof(uint32_t);
    ctx->video_width = src_width;
    ctx->video_height = src_height;

    // Provide audio samples
    if (g_audio_frame_count > 0) {
        ctx->audio_buffer = g_audio_buffer;
        ctx->audio_frames = (uint32_t)g_audio_frame_count;
    } else {
        ctx->audio_buffer = nullptr;
        ctx->audio_frames = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_reset(void* core_instance) {
    HatariCore* core = (HatariCore*)core_instance;
    if (!core || !core->initialized)
        return;
    fl_log_info("Soft reset");
    // hatariB's retro_reset does warm or cold depending on core_option_soft_reset
    retro_reset();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_hard_reset(void* core_instance) {
    HatariCore* core = (HatariCore*)core_instance;
    if (!core || !core->initialized)
        return;
    fl_log_info("Hard reset");
    retro_reset();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_get_video_spec(void* core_instance, RpVideoSpec* spec) {
    HatariCore* core = (HatariCore*)core_instance;
    if (core) {
        *spec = core->video_spec;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_get_audio_spec(void* core_instance, RpAudioSpec* spec) {
    HatariCore* core = (HatariCore*)core_instance;
    if (core) {
        *spec = core->audio_spec;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RpEmuLifecycleState hatari_get_state(void* core_instance) {
    HatariCore* core = (HatariCore*)core_instance;
    return core ? core->state : RpEmuLifecycleState_Idle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static size_t hatari_serialize(void* core_instance, void* buffer, size_t buffer_size) {
    HatariCore* core = (HatariCore*)core_instance;
    if (!core || !core->initialized)
        return 0;

    if (buffer == nullptr)
        return retro_serialize_size();

    return retro_serialize(buffer, buffer_size) ? buffer_size : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool hatari_deserialize(void* core_instance, const void* buffer, size_t buffer_size) {
    HatariCore* core = (HatariCore*)core_instance;
    if (!core || !core->initialized)
        return false;
    return retro_unserialize(buffer, buffer_size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_set_config_int(void* core_instance, FlString key, int32_t value) {
    (void)core_instance;
    fl_log_debug("Set config int: %.*s = %d", (int)key.length, key.data, value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_set_config_float(void* core_instance, FlString key, float value) {
    (void)core_instance;
    fl_log_debug("Set config float: %.*s = %f", (int)key.length, key.data, (double)value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_set_config_bool(void* core_instance, FlString key, bool value) {
    (void)core_instance;
    fl_log_debug("Set config bool: %.*s = %s", (int)key.length, key.data, value ? "true" : "false");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hatari_set_config_string(void* core_instance, FlString key, FlString value) {
    (void)core_instance;
    fl_log_debug("Set config string: %.*s = %.*s", (int)key.length, key.data, (int)value.length, value.data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString hatari_get_config_string(void* core_instance, FlString key) {
    HatariCore* core = (HatariCore*)core_instance;
    if (!core)
        return (FlString) { 0 };

    if (string_equals(key, string_from_cstr("FLOPPY_A"))) {
        return core->disk_path;
    }
    return (FlString) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RpEmuAPI s_hatari_api = {
    .get_info = hatari_get_info,
    .create = hatari_create,
    .destroy = hatari_destroy,
    .mount_media = hatari_mount_media,
    .unmount_media = hatari_unmount_media,
    .mount_media_list = NULL,
    .run_frame = hatari_run_frame,
    .reset = hatari_reset,
    .hard_reset = hatari_hard_reset,
    .get_video_spec = hatari_get_video_spec,
    .get_audio_spec = hatari_get_audio_spec,
    .get_state = hatari_get_state,
    .copy_audio = NULL,
    .serialize = hatari_serialize,
    .deserialize = hatari_deserialize,
    .set_config_bitmask = NULL,
    .set_config_int = hatari_set_config_int,
    .set_config_float = hatari_set_config_float,
    .set_config_bool = hatari_set_config_bool,
    .set_config_string = hatari_set_config_string,
    .get_config_string = hatari_get_config_string,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void) {
    return &s_hatari_api;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
