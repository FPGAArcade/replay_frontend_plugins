///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VICE C64 Core Plugin - Wrapper around VICE C64 emulator
//
// This plugin wraps the vice-libretro C64 emulator into the Replay Core Plugin API.
// The libretro layer handles the actual emulation, we just adapt the interface.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <flowi/arena/arena.h>
#include <flowi/arena/arena_macros.h>
#include "replay/emu_plugin.h"
#include <flowi/core/log_macros.h>
#include "replay/plugin_info.h"
#include <flowi/core/log_macros.h>
#include <flowi/string/string.h>
#include <flowi/core/types.h>

// Libretro types and constants
#include "libretro-common/include/libretro.h"

// VICE internal headers for warp control and resources
#include "libretro/libretro-dc.h"
#include "libretro/libretro-glue.h"
#include "vice/src/vsync.h"

// VICE libretro functions for proper game reload
// reload_restart() sets up autostart from full_path via update_from_vice()
extern void reload_restart(void);
extern bool retro_disk_set_image_index(unsigned index);
extern bool retro_disk_set_eject_state(bool ejected);
extern dc_storage* dc;
extern unsigned int vice_drive_halftrack[];

#include <file/file_path.h>

#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Libretro interface declarations (from libretro-core.c)
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Libretro standard functions
void retro_init(void);
void retro_deinit(void);
void retro_reset(void);
void retro_run(void);
bool retro_load_game(const struct retro_game_info* info);
void retro_unload_game(void);
void retro_get_system_av_info(struct retro_system_av_info* info);
size_t retro_serialize_size(void);
bool retro_serialize(void* data, size_t size);
bool retro_unserialize(const void* data, size_t size);

// Callback setters
void retro_set_environment(retro_environment_t cb);
void retro_set_video_refresh(retro_video_refresh_t cb);
void retro_set_input_poll(retro_input_poll_t cb);
void retro_set_input_state(retro_input_state_t cb);
void retro_set_audio_sample(retro_audio_sample_t cb);
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb);

// Video dimensions for C64: 384x288 (PAL), 384x247 (NTSC)
// Must match VICE's libretro-core.h definitions
#define WINDOW_WIDTH 384
#define WINDOW_HEIGHT 288
#define RETRO_BMP_SIZE (WINDOW_WIDTH * WINDOW_HEIGHT * 2)
extern unsigned short int retro_bmp[RETRO_BMP_SIZE];
extern unsigned short int pix_bytes;

// Frame dimensions (may change based on cropping)
extern unsigned int retrow;
extern unsigned int retroh;

// Region (PAL/NTSC)
extern unsigned int retro_region;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Global state for libretro callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const RpEmuInputState* g_current_input = nullptr;

// Audio buffer - ~2 frames worth at 44100Hz/50fps = ~1764 samples/frame, stereo = 3528 floats
#define AUDIO_BUFFER_MAX_FRAMES 4096
static float g_audio_buffer[AUDIO_BUFFER_MAX_FRAMES * 2];
static size_t g_audio_frame_count = 0;

// Video buffer for XRGB8888 conversion
static uint32_t g_frame_buffer[WINDOW_WIDTH * WINDOW_HEIGHT];

// Video callback data
static const void* g_video_data = nullptr;
static unsigned g_video_width = 0;
static unsigned g_video_height = 0;
static size_t g_video_pitch = 0;
static enum retro_pixel_format g_pixel_format = RETRO_PIXEL_FORMAT_RGB565;

// System/save directories for libretro
static char g_system_directory[4096] = ".";
static char g_save_directory[4096] = ".";

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Libretro callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void RETRO_CALLCONV vice_log_printf(enum retro_log_level level, const char* fmt, ...) {
    (void)level;
    va_list args;
    va_start(args, fmt);
    // Format to a buffer and log via our API
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Remove trailing newline if present
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }

    fl_log_debug("VICE: %s", buffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool vice_environment(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            struct retro_log_callback* cb = (struct retro_log_callback*)data;
            cb->log = vice_log_printf;
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
            // We support RGB565 (convert to XRGB8888) and XRGB8888 (pass through)
            if (*fmt == RETRO_PIXEL_FORMAT_RGB565 || *fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
                g_pixel_format = *fmt;
                fl_log_info("VICE requested pixel format: %s",
                            *fmt == RETRO_PIXEL_FORMAT_RGB565 ? "RGB565" : "XRGB8888");
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
            // Provide default values for critical core options
            if (var->key) {
                if (strcmp(var->key, "vice_sound_sample_rate") == 0) {
                    var->value = "48000";
                    return true;
                }
                if (strcmp(var->key, "vice_sid_model") == 0) {
                    // "default" uses the C64 model to determine SID:
                    // C64 = 6581, C64C = 8580
                    var->value = "default";
                    return true;
                }
                if (strcmp(var->key, "vice_sid_engine") == 0) {
                    // ReSID-FP provides the best quality SID emulation
                    var->value = "ReSID-FP";
                    return true;
                }
                if (strcmp(var->key, "vice_sid_extra") == 0) {
                    var->value = "disabled"; // No second SID
                    return true;
                }
                if (strcmp(var->key, "vice_drive_true_emulation") == 0) {
                    // TDE enabled - required for demos with custom loaders/fastloaders
                    // Fast startup (TDE disabled initially) doesn't work with most demos
                    // as their custom loaders need accurate 1541 timing from the start
                    var->value = "enabled";
                    return true;
                }
                if (strcmp(var->key, "vice_autostart") == 0) {
                    var->value = "warp"; // Warp during autostart loading
                    return true;
                }
                if (strcmp(var->key, "vice_autoloadwarp") == 0) {
                    var->value = "enabled"; // Warp during disk/tape access when no audio
                    return true;
                }
                if (strcmp(var->key, "vice_warp_boost") == 0) {
                    var->value = "enabled"; // Use FastSID during warp for max loading speed
                    return true;
                }
                if (strcmp(var->key, "vice_reset") == 0) {
                    var->value = "autostart";
                    return true;
                }
                if (strcmp(var->key, "vice_external_palette") == 0) {
                    var->value = "default"; // Use internal palette
                    return true;
                }
                if (strcmp(var->key, "vice_floppy_multidrive") == 0) {
                    var->value = "disabled"; // Only use drive 8
                    return true;
                }
                if (strcmp(var->key, "vice_work_disk") == 0) {
                    var->value = "disabled"; // No work disk (prevents drive 9 conflicts)
                    return true;
                }
                if (strcmp(var->key, "vice_ram_expansion_unit") == 0) {
                    var->value = "none"; // No REU - standard C64 memory only
                    return true;
                }
                if (strcmp(var->key, "vice_cartridge") == 0) {
                    var->value = "none"; // No cartridge
                    return true;
                }
                if (strcmp(var->key, "vice_c64_model") == 0) {
                    // C64C PAL auto - uses 8580 SID which is better for demos from late 90s+
                    // "auto" switches region based on file path tags
                    var->value = "C64C PAL auto";
                    return true;
                }
                if (strcmp(var->key, "vice_virtual_device_traps") == 0) {
                    // Enabled for fast loading when TDE is disabled
                    // Will be effectively disabled when TDE is enabled (TDE takes precedence)
                    var->value = "enabled";
                    return true;
                }
                if (strcmp(var->key, "vice_gfx_colors") == 0) {
                    var->value = "16bit"; // Use RGB565 for better compatibility
                    return true;
                }
                // VICII color settings - use proper defaults (1000 = 100%)
                if (strcmp(var->key, "vice_vicii_color_saturation") == 0) {
                    var->value = "1000";
                    return true;
                }
                if (strcmp(var->key, "vice_vicii_color_contrast") == 0) {
                    var->value = "1000";
                    return true;
                }
                if (strcmp(var->key, "vice_vicii_color_brightness") == 0) {
                    var->value = "1000";
                    return true;
                }
                if (strcmp(var->key, "vice_vicii_color_gamma") == 0) {
                    var->value = "2800"; // 2.8 gamma, default for PAL
                    return true;
                }
                if (strcmp(var->key, "vice_vicii_color_tint") == 0) {
                    var->value = "1000";
                    return true;
                }
                // Return NULL for unknown options (use core defaults)
            }
            var->value = NULL;
            return false;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
            bool* update = (bool*)data;
            *update = false;
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
            *version = 0;
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

        default:
            // fl_log_debug("Unhandled libretro environment cmd: %u", cmd);
            return false;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_video_refresh(const void* data, unsigned width, unsigned height, size_t pitch) {
    if (data == nullptr) {
        return;
    }
    g_video_data = data;
    g_video_width = width;
    g_video_height = height;
    g_video_pitch = pitch;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_input_poll(void) {
    // Nothing to do - we update g_current_input before calling retro_run
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int16_t vice_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)index;

    // Only handle joypad on port 1 (C64 joystick port 2, which is the common gaming port)
    if (device != RETRO_DEVICE_JOYPAD || port != 1) {
        return 0;
    }

    if (!g_current_input) {
        return 0;
    }

    // Map libretro joypad IDs to RpEmuButton
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
            // C64 joystick has one fire button - map both A and B to it
            return (g_current_input->buttons[RpEmuButton_A] || g_current_input->buttons[RpEmuButton_B]) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_START:
            return g_current_input->buttons[RpEmuButton_Start] ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_SELECT:
            return g_current_input->buttons[RpEmuButton_Select] ? 1 : 0;
        default:
            return 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static size_t vice_audio_batch(const int16_t* data, size_t frames) {
    // Don't overflow the buffer
    if (frames > AUDIO_BUFFER_MAX_FRAMES - g_audio_frame_count) {
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

static void vice_audio_sample(int16_t left, int16_t right) {
    int16_t samples[2] = { left, right };
    vice_audio_batch(samples, 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core instance
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ViceC64Core {
    FlArena* arena;
    RpEmuLifecycleState state;
    RpVideoSpec video_spec;
    RpAudioSpec audio_spec;
    FlString disk_path;
    bool initialized;
    // Fast load: TDE disabled during initial load, enabled when warp turns off
    bool tde_enabled;
    bool warp_was_on;

    // Multi-disk auto-swap state
    FlString* media_list;
    uint32_t media_count;
    uint32_t current_disk_index;
    bool auto_swap_enabled;

    // Track-return detection state for triggering next disk swap
    bool swap_arm_reached_far_track;
    int32_t max_halftrack_seen;
    uint32_t track0_streak;
    uint32_t hightrack_stall_streak;
    int32_t swap_last_halftrack;
    uint32_t swap_cooldown_frames;

    // Debug logging state (throttled)
    uint32_t debug_frame_counter;
} ViceC64Core;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_reset_swap_detection(ViceC64Core* core) {
    core->swap_arm_reached_far_track = false;
    core->max_halftrack_seen = 0;
    core->track0_streak = 0;
    core->hightrack_stall_streak = 0;
    core->swap_last_halftrack = -1;
    core->swap_cooldown_frames = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool vice_copy_rpstring_path(FlString path, char* out, size_t out_size) {
    if (!out || out_size == 0 || !path.data) {
        return false;
    }
    if (path.length + 1 > out_size) {
        return false;
    }
    memcpy(out, path.data, path.length);
    out[path.length] = '\0';
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int vice_get_drive8_halftrack(void) {
    return (int)vice_drive_halftrack[0];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_apply_disk_index(ViceC64Core* core, uint32_t index) {
    if (dc == nullptr || index >= core->media_count) {
        return;
    }

    if (!retro_disk_set_eject_state(true)) {
        fl_log_warning("Auto-swap: failed to eject before switching to disk %u", index + 1);
    }
    if (!retro_disk_set_image_index(index)) {
        fl_log_warning("Auto-swap: failed to select disk index %u", index);
    }
    if (!retro_disk_set_eject_state(false)) {
        fl_log_warning("Auto-swap: failed to insert disk %u", index + 1);
    }

    fl_log_info("Auto-swap: disk index applied -> %u/%u", index + 1, core->media_count);
    core->current_disk_index = index;
    core->disk_path = core->media_list[index];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_enter_fast_loading_mode(ViceC64Core* core, const char* reason) {
    (void)reason;
    vsync_set_warp_mode(1);
    core->warp_was_on = true;
    core->tde_enabled = false;
    fl_log_info("Warp mode enabled for fast loading (TDE disabled)");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_configure_fliplist_from_media_list(ViceC64Core* core) {
    if (dc == nullptr) {
        fl_log_warning("VICE disk control context unavailable; multi-disk auto-swap disabled");
        return;
    }

    dc_reset(dc);
    dc->unit = 8;

    for (uint32_t i = 0; i < core->media_count; i++) {
        char path[4096];
        if (!vice_copy_rpstring_path(core->media_list[i], path, sizeof(path))) {
            fl_log_warning("Skipping disk %u in fliplist: path too long or invalid", i + 1);
            continue;
        }
        if (!dc_add_file(dc, path, nullptr, nullptr, nullptr)) {
            fl_log_warning("Failed to append disk %u to VICE fliplist: %s", i + 1, path);
        } else {
            fl_log_info("Fliplist: added disk %u/%u: %s", i + 1, core->media_count, path);
        }
    }

    dc->index = 0;
    dc->index_prev = 0;
    dc->eject_state = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_c64_get_info(RpEmuInfo* info) {
    info->emu_name = "VICE";
    info->emu_version = "3.9";
    info->system_name = "Commodore 64";
    info->supported_extensions = "d64|d71|d80|d81|d82|g64|g41|x64|t64|tap|prg|p00|crt|bin|m3u|vsf|nib|nbz";
    info->requires_bios = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// VICE reads its ROMs from <system directory>/vice/, and they ship in data/ beside this plugin.
// Not realpath: VICE stubs it out, and -Bsymbolic binds this call to the stub.
static bool vice_find_system_directory(void) {
    Dl_info info = { 0 };
    const char* slash = nullptr;
    if (dladdr((void*)&vice_find_system_directory, &info) && info.dli_fname) {
        slash = strrchr(info.dli_fname, '/');
    }
    if (!slash) {
        fl_log_error("VICE: cannot locate the plugin, so its data/ folder and the ROMs in it are not found");
        return false;
    }
    snprintf(g_system_directory, sizeof(g_system_directory), "%.*s/data", (int)(slash - info.dli_fname),
             info.dli_fname);
    if (!path_is_directory(g_system_directory)) {
        fl_log_error("VICE: no data folder at %s, so the ROMs are not found", g_system_directory);
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* vice_c64_create(FlArena* arena) {

    fl_log_info("Creating VICE C64 core");

    // VICE reads the system directory in retro_set_environment, so it must be known first. Without
    // its ROMs VICE fails to start and then double-frees on the way out, so refuse here instead.
    if (!vice_find_system_directory()) {
        return nullptr;
    }

    ViceC64Core* core = arena_alloc_zero(arena, ViceC64Core);
    core->arena = arena;
    core->state = RpEmuLifecycleState_Idle;
    core->initialized = false;

    // Set up libretro callbacks before init
    // Environment callback MUST be set first - it's called during retro_init
    retro_set_environment(vice_environment);
    retro_set_video_refresh(vice_video_refresh);
    retro_set_input_poll(vice_input_poll);
    retro_set_input_state(vice_input_state);
    retro_set_audio_sample_batch(vice_audio_batch);
    retro_set_audio_sample(vice_audio_sample);

    retro_init();
    core->initialized = true;

    // Set up video spec (PAL by default)
    // VICE outputs 384x288 for PAL C64
    core->video_spec.width = WINDOW_WIDTH;
    core->video_spec.height = WINDOW_HEIGHT;
    core->video_spec.pixel_format = RpPixelFormat_Xrgb8888;
    core->video_spec.fps = 50.0f;
    // Visible region - show full frame for now
    core->video_spec.visible_x = 0;
    core->video_spec.visible_y = 0;
    core->video_spec.visible_width = WINDOW_WIDTH;
    core->video_spec.visible_height = WINDOW_HEIGHT;

    // Set up audio spec
    core->audio_spec.sample_rate = 44100;
    core->audio_spec.channels = 2;
    core->auto_swap_enabled = false;
    core->debug_frame_counter = 0;
    vice_reset_swap_detection(core);

    fl_log_info("VICE C64 core created");
    return core;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_c64_destroy(void* core_instance) {
    if (!core_instance)
        return;

    ViceC64Core* core = (ViceC64Core*)core_instance;
    fl_log_info("Destroying VICE C64 core");

    if (core->initialized) {
        retro_unload_game();
        retro_deinit();
        core->initialized = false;
    }

    g_current_input = nullptr;
    g_audio_frame_count = 0;
    core->media_list = nullptr;
    core->media_count = 0;
    core->current_disk_index = 0;
    core->auto_swap_enabled = false;
    vice_reset_swap_detection(core);
    core->state = RpEmuLifecycleState_Idle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool vice_c64_mount_media(void* core_instance, const char* path) {
    ViceC64Core* core = (ViceC64Core*)core_instance;

    if (!core || !core->initialized) {
        fl_log_error("Invalid core instance");
        return false;
    }

    fl_log_info("Mounting media: %s", path);

    struct retro_game_info game_info = { 0 };
    game_info.path = path;

    if (!retro_load_game(&game_info)) {
        fl_log_error("Failed to load game: %s", path);
        return false;
    }

    // Call reload_restart() to properly set up autostart
    // This ensures autostartString is set from full_path via update_from_vice()
    // Critical for second game loads where the normal RUNSTATE_FIRST_START path isn't taken
    reload_restart();

    // Enable warp mode immediately for fast loading
    // The VICE libretro core will disable it when audio plays or loading completes
    vice_enter_fast_loading_mode(core, "initial mount");
    core->media_list = nullptr;
    core->media_count = 0;
    core->current_disk_index = 0;
    vice_reset_swap_detection(core);

    struct retro_system_av_info av_info = { 0 };
    retro_get_system_av_info(&av_info);

    core->video_spec.width = av_info.geometry.base_width;
    core->video_spec.height = av_info.geometry.base_height;
    core->video_spec.fps = (float)av_info.timing.fps;

    if (av_info.timing.sample_rate > 0) {
        core->audio_spec.sample_rate = (uint32_t)av_info.timing.sample_rate;
    }

    core->disk_path = string_copy(core->arena, string_from_cstr(path));
    core->state = RpEmuLifecycleState_Running;

    fl_log_info("Media mounted: %ux%u @ %.2f fps, audio: %u Hz", core->video_spec.width, core->video_spec.height,
                core->video_spec.fps, core->audio_spec.sample_rate);

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool vice_c64_mount_media_list(void* core_instance, const FlString* paths, uint32_t count) {
    ViceC64Core* core = (ViceC64Core*)core_instance;

    if (!core || !core->initialized || !paths || count == 0) {
        fl_log_error("Invalid parameters for mount_media_list");
        return false;
    }

    char first_path[4096];
    if (!vice_copy_rpstring_path(paths[0], first_path, sizeof(first_path))) {
        fl_log_error("First disk path too long or invalid");
        return false;
    }
    if (!vice_c64_mount_media(core_instance, first_path)) {
        return false;
    }

    core->media_list = arena_alloc_array(core->arena, FlString, count);
    for (uint32_t i = 0; i < count; i++) {
        core->media_list[i] = string_copy(core->arena, paths[i]);
    }
    core->media_count = count;
    core->current_disk_index = 0;
    vice_reset_swap_detection(core);

    vice_configure_fliplist_from_media_list(core);
    vice_apply_disk_index(core, 0);

    fl_log_info("Mounted media list: %u disk(s), auto_swap=%s, current=1", count,
                core->auto_swap_enabled ? "true" : "false");
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_c64_unmount_media(void* core_instance) {
    ViceC64Core* core = (ViceC64Core*)core_instance;

    if (!core || !core->initialized)
        return;

    fl_log_info("Unmounting media");

    // Reset TDE to disabled for next load's fast loading phase
    // This ensures the next game starts with TDE off for fast KERNAL load
    // Only touch Drive8 - Drive9 should stay disabled to avoid breaking demos
    log_resources_set_int("Drive8TrueEmulation", 0);
    log_resources_set_int("VirtualDevice8", 1);

    retro_unload_game();

    // Reset our tracking state for next game
    core->tde_enabled = false;
    core->warp_was_on = false;
    core->media_list = nullptr;
    core->media_count = 0;
    core->current_disk_index = 0;
    vice_reset_swap_detection(core);
    core->state = RpEmuLifecycleState_Idle;
    core->disk_path = (FlString) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_c64_run_frame(void* core_instance, RpEmuFrameContext* ctx) {
    ViceC64Core* core = (ViceC64Core*)core_instance;

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

    bool warp_is_on = vsync_get_warp_mode() != 0;
    ctx->skip_frame_pacing = warp_is_on;
    if (core->warp_was_on && !warp_is_on && !core->tde_enabled) {
        // Legacy behavior: when warp turns off, loading is considered complete.
        log_resources_set_int("Drive8TrueEmulation", 1);
        log_resources_set_int("VirtualDevice8", 0);
        core->tde_enabled = true;
        fl_log_info("Loading complete (warp transition) - TDE enabled for accurate emulation");
    }
    core->warp_was_on = warp_is_on;

    // Multi-disk auto-swap: detect a "far track -> return to track 0" pattern.
    // This approximates demo part transitions where the next side/disk is requested.
    if (core->auto_swap_enabled && core->media_count > 1 && core->current_disk_index + 1 < core->media_count) {
        core->debug_frame_counter++;
        if (core->swap_cooldown_frames > 0) {
            core->swap_cooldown_frames--;
        }

        int halftrack = vice_get_drive8_halftrack();
        if (halftrack >= 0) {
            if (halftrack > core->max_halftrack_seen) {
                core->max_halftrack_seen = halftrack;
            }

            // Require meaningful drive travel first to avoid false positives near boot.
            if (core->max_halftrack_seen >= 36) {
                if (!core->swap_arm_reached_far_track) {
                    core->swap_arm_reached_far_track = true;
                    fl_log_info("Auto-swap: armed (max_halftrack=%d)", core->max_halftrack_seen);
                }
            }

            if (core->swap_arm_reached_far_track && core->tde_enabled && core->swap_cooldown_frames == 0) {
                int delta = halftrack - core->swap_last_halftrack;
                if (delta < 0) {
                    delta = -delta;
                }
                bool near_last_track = (core->swap_last_halftrack >= 0) && (delta <= 1);
                // Some loaders wait a few halftracks below the peak seek point (e.g. max=39, wait=36).
                // Once armed, accept sustained near-static high-track activity as a swap request.
                if (near_last_track && halftrack >= 30) {
                    core->hightrack_stall_streak++;
                } else {
                    core->hightrack_stall_streak = 0;
                }
                core->swap_last_halftrack = halftrack;

                if (halftrack <= 1) {
                    core->track0_streak++;
                } else {
                    core->track0_streak = 0;
                }

                // Trigger either on track-0 return or prolonged stall on high track.
                bool trigger_track0 = core->track0_streak >= 3;
                bool trigger_stall = core->hightrack_stall_streak >= 180;
                if (trigger_track0 || trigger_stall) {
                    uint32_t next = core->current_disk_index + 1;
                    fl_log_info(
                        "Auto-swap trigger: reason=%s streak0=%u stall=%u ht=%d max=%d next=%u/%u warp=%d tde=%d",
                        trigger_track0 ? "track0" : "stall", core->track0_streak, core->hightrack_stall_streak,
                        halftrack, core->max_halftrack_seen, next + 1, core->media_count, warp_is_on ? 1 : 0,
                        core->tde_enabled ? 1 : 0);
                    vice_apply_disk_index(core, next);
                    fl_log_info("Auto-swap: switched to disk %u/%u", next + 1, core->media_count);

                    vice_reset_swap_detection(core);
                    core->swap_cooldown_frames = 150; // ~3 seconds at 50 fps
                }
            }

            if ((core->debug_frame_counter % 300) == 0) {
                fl_log_debug("Auto-swap state: idx=%u/%u enabled=%d warp=%d tde=%d ht=%d max=%d arm=%d streak0=%u "
                             "stall=%u cooldown=%u",
                             core->current_disk_index + 1, core->media_count, core->auto_swap_enabled ? 1 : 0,
                             warp_is_on ? 1 : 0, core->tde_enabled ? 1 : 0, halftrack, core->max_halftrack_seen,
                             core->swap_arm_reached_far_track ? 1 : 0, core->track0_streak,
                             core->hightrack_stall_streak, core->swap_cooldown_frames);
            }
        }
    }

    // Get source dimensions
    unsigned src_width = g_video_width > 0 ? g_video_width : (retrow > 0 ? retrow : WINDOW_WIDTH);
    unsigned src_height = g_video_height > 0 ? g_video_height : (retroh > 0 ? retroh : WINDOW_HEIGHT);

    if (src_width > 0 && src_height > 0) {
        if (g_pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
            // XRGB8888 (0xXXRRGGBB) -> RGBA32 (SDL_PIXELFORMAT_RGBA32 = ABGR8888 on little-endian)
            // Need to swap R and B channels: 0xXXRRGGBB -> 0xFFBBGGRR
            const uint32_t* src = g_video_data ? (const uint32_t*)g_video_data : (const uint32_t*)retro_bmp;
            size_t src_pitch_pixels = g_video_pitch > 0 ? (g_video_pitch / 4) : src_width;

            for (unsigned y = 0; y < src_height && y < WINDOW_HEIGHT; y++) {
                const uint32_t* src_row = src + y * src_pitch_pixels;
                uint32_t* dst_row = g_frame_buffer + y * WINDOW_WIDTH;
                for (unsigned x = 0; x < src_width && x < WINDOW_WIDTH; x++) {
                    uint32_t pixel = src_row[x];
                    // Extract RGB from XRGB8888 (0x00RRGGBB)
                    uint8_t r = (pixel >> 16) & 0xFF;
                    uint8_t g = (pixel >> 8) & 0xFF;
                    uint8_t b = pixel & 0xFF;
                    // Output as ABGR8888: u32 = 0xAABBGGRR -> memory [R, G, B, A]
                    dst_row[x] = (0xFFu << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
                }
            }
        } else {
            // RGB565: Convert to RGBA32 (SDL_PIXELFORMAT_RGBA32 = ABGR8888 on little-endian)
            // Memory order must be [R, G, B, A], so u32 value is 0xAABBGGRR
            const uint16_t* src = g_video_data ? (const uint16_t*)g_video_data : retro_bmp;
            size_t src_pitch_pixels = g_video_pitch > 0 ? (g_video_pitch / 2) : src_width;

            for (unsigned y = 0; y < src_height && y < WINDOW_HEIGHT; y++) {
                const uint16_t* src_row = src + y * src_pitch_pixels;
                uint32_t* dst_row = g_frame_buffer + y * WINDOW_WIDTH;
                for (unsigned x = 0; x < src_width && x < WINDOW_WIDTH; x++) {
                    uint16_t pixel = src_row[x];
                    // RGB565: RRRRRGGGGGGBBBBB
                    uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                    uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                    uint8_t b = (pixel & 0x1F) << 3;
                    // Expand to full range
                    r |= r >> 5;
                    g |= g >> 6;
                    b |= b >> 5;
                    // Output as ABGR8888: u32 = 0xAABBGGRR -> memory [R, G, B, A]
                    dst_row[x] = (0xFFu << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
                }
            }
        }

        // Update video spec dimensions if changed
        if (core->video_spec.width != g_video_width || core->video_spec.height != g_video_height) {
            core->video_spec.width = g_video_width;
            core->video_spec.height = g_video_height;
        }
    }

    ctx->video_buffer = g_frame_buffer;
    ctx->video_pitch = WINDOW_WIDTH * sizeof(uint32_t);
    ctx->video_width = src_width;
    ctx->video_height = src_height;

    // Provide audio samples
    // Note: Warp mode is handled by the VICE libretro core via AutostartWarp and autoloadwarp options.
    // The core automatically enables warp during disk/tape loading and disables it when audio is detected.
    if (g_audio_frame_count > 0) {
        ctx->audio_buffer = g_audio_buffer;
        ctx->audio_frames = (uint32_t)g_audio_frame_count;
    } else {
        ctx->audio_buffer = nullptr;
        ctx->audio_frames = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_c64_reset(void* core_instance) {
    ViceC64Core* core = (ViceC64Core*)core_instance;
    if (!core || !core->initialized)
        return;
    fl_log_info("Soft reset");
    retro_reset();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_c64_hard_reset(void* core_instance) {
    ViceC64Core* core = (ViceC64Core*)core_instance;
    if (!core || !core->initialized)
        return;
    fl_log_info("Hard reset");
    retro_reset();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_c64_get_video_spec(void* core_instance, RpVideoSpec* spec) {
    ViceC64Core* core = (ViceC64Core*)core_instance;
    if (core) {
        *spec = core->video_spec;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_c64_get_audio_spec(void* core_instance, RpAudioSpec* spec) {
    ViceC64Core* core = (ViceC64Core*)core_instance;
    if (core) {
        *spec = core->audio_spec;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RpEmuLifecycleState vice_c64_get_state(void* core_instance) {
    ViceC64Core* core = (ViceC64Core*)core_instance;
    return core ? core->state : RpEmuLifecycleState_Idle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static size_t vice_c64_serialize(void* core_instance, void* buffer, size_t buffer_size) {
    ViceC64Core* core = (ViceC64Core*)core_instance;
    if (!core || !core->initialized)
        return 0;

    if (buffer == nullptr)
        return retro_serialize_size();

    return retro_serialize(buffer, buffer_size) ? buffer_size : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool vice_c64_deserialize(void* core_instance, const void* buffer, size_t buffer_size) {
    ViceC64Core* core = (ViceC64Core*)core_instance;
    if (!core || !core->initialized)
        return false;
    return retro_unserialize(buffer, buffer_size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_c64_set_config_int(void* core_instance, FlString key, int32_t value) {
    (void)core_instance;
    fl_log_debug("Set config int: %.*s = %d", (int)key.length, key.data, value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_c64_set_config_float(void* core_instance, FlString key, float value) {
    (void)core_instance;
    fl_log_debug("Set config float: %.*s = %f", (int)key.length, key.data, (double)value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_c64_set_config_bool(void* core_instance, FlString key, bool value) {
    ViceC64Core* core = (ViceC64Core*)core_instance;
    if (!core) {
        return;
    }

    if (string_equals(key, string_from_cstr("AUTO_SWAP_DISKS"))) {
        core->auto_swap_enabled = value;
        fl_log_info("Auto-swap disks: %s (media_count=%u current=%u)", value ? "enabled" : "disabled",
                    core->media_count, core->current_disk_index + 1);
        return;
    }

    fl_log_debug("Set config bool: %.*s = %s", (int)key.length, key.data, value ? "true" : "false");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vice_c64_set_config_string(void* core_instance, FlString key, FlString value) {
    (void)core_instance;
    fl_log_debug("Set config string: %.*s = %.*s", (int)key.length, key.data, (int)value.length, value.data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString vice_c64_get_config_string(void* core_instance, FlString key) {
    ViceC64Core* core = (ViceC64Core*)core_instance;
    if (!core)
        return (FlString) { 0 };

    if (string_equals(key, string_from_cstr("DRIVE8"))) {
        if (core->media_count > 0 && core->current_disk_index < core->media_count) {
            return core->media_list[core->current_disk_index];
        }
        return core->disk_path;
    }
    return (FlString) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RpEmuAPI s_vice_c64_api = {
    .get_info = vice_c64_get_info,
    .create = vice_c64_create,
    .destroy = vice_c64_destroy,
    .mount_media = vice_c64_mount_media,
    .unmount_media = vice_c64_unmount_media,
    .mount_media_list = vice_c64_mount_media_list,
    .run_frame = vice_c64_run_frame,
    .reset = vice_c64_reset,
    .hard_reset = vice_c64_hard_reset,
    .get_video_spec = vice_c64_get_video_spec,
    .get_audio_spec = vice_c64_get_audio_spec,
    .get_state = vice_c64_get_state,
    .copy_audio = NULL,
    .serialize = vice_c64_serialize,
    .deserialize = vice_c64_deserialize,
    .set_config_bitmask = NULL,
    .set_config_int = vice_c64_set_config_int,
    .set_config_float = vice_c64_set_config_float,
    .set_config_bool = vice_c64_set_config_bool,
    .set_config_string = vice_c64_set_config_string,
    .get_config_string = vice_c64_get_config_string,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void) {
    return &s_vice_c64_api;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ABI version export. The host resolves this before it reads the vtable above and refuses the
// plugin when the answer is not the version it was built for.

RP_EMU_PLUGIN_ABI_VERSION_EXPORT()

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
