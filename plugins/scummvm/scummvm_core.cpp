///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ScummVM Core Plugin - Wrapper around ScummVM engine
//
// This plugin wraps the ScummVM engine into the Replay Core Plugin API.
// Uses native OSystem backend with libco coroutines for frame-based execution.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <flowi/arena/arena.h>
#include "replay/emu_plugin.h"
#include "replay/plugin_info.h"
#include <flowi/core/log_macros.h>
#include <flowi/string/string.h>

#include "common/events.h"
#include "common/keyboard.h"
#include "replay_osystem.h"
#include "replay_graphics.h"
#include "replay_mixer.h"

#include <cstdio>
#include <cstring>
#include <strings.h> // for strcasecmp

// Logging helpers over the exported host log API (resolved at dlopen like every host symbol)
#define SCUMMVM_LOG(core, fmt, ...)  \
    do {                             \
        (void)(core);                \
        fl_log_info(fmt, ##__VA_ARGS__); \
    } while (0)

#define SCUMMVM_ERROR(core, fmt, ...) \
    do {                              \
        (void)(core);                 \
        fl_log_error(fmt, ##__VA_ARGS__); \
    } while (0)

#define SCUMMVM_DEBUG(core, fmt, ...) \
    do {                              \
        (void)(core);                 \
        fl_log_debug(fmt, ##__VA_ARGS__); \
    } while (0)

// Forward declaration for ScummVM global system
extern OSystem* g_system;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core instance - wraps ScummVM

struct ScummVMCore {
    FlArena* arena;
    OSystem_Replay* osystem;
    RpEmuLifecycleState state;

    // Video spec
    RpVideoSpec video_spec;

    // Audio spec
    RpAudioSpec audio_spec;

    // Audio buffer for float conversion
    float* audio_float_buffer;
    uint32_t audio_buffer_size;

    // Game info
    char game_path[1024];
    char game_id[256];

    // Track if coroutines have been initialized
    bool coroutines_initialized;


    // Input state tracking
    RpEmuInputState prev_input; // Previous frame's input for edge detection
    int32_t mouse_x;             // Accumulated mouse X position
    int32_t mouse_y;             // Accumulated mouse Y position
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get core information

static void scummvm_get_info(RpEmuInfo* info) {
    info->emu_name = "ScummVM";
    info->emu_version = "2.9.0";
    info->system_name = "Adventure Games";
    info->supported_extensions = "scummvm"; // Uses directory-based detection
    info->requires_bios = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create core instance

static void* scummvm_create(FlArena* arena) {
    fl_log_info("Creating ScummVM core");
    ScummVMCore* core = (ScummVMCore*)arena_alloc_raw_zero(arena, sizeof(ScummVMCore), alignof(ScummVMCore));
    core->arena = arena;
    core->state = RpEmuLifecycleState_Idle;

    // Create OSystem_Replay
    core->osystem = new OSystem_Replay();
    g_system = core->osystem;

    // Initialize backend
    core->osystem->initBackend();

    // Coroutines will be initialized when a game is mounted
    core->coroutines_initialized = false;

    // Initialize input state
    memset(&core->prev_input, 0, sizeof(core->prev_input));
    core->mouse_x = 160; // Start mouse in center of default 320x200 screen
    core->mouse_y = 100;

    // Set default video spec (ScummVM games typically start at 320x200)
    core->video_spec.width = 320;
    core->video_spec.height = 200;
    core->video_spec.pixel_format = RpPixelFormat_Xrgb8888;
    core->video_spec.fps = 60.0f;
    core->video_spec.visible_x = 0;
    core->video_spec.visible_y = 0;
    core->video_spec.visible_width = 320;
    core->video_spec.visible_height = 200;

    // Set audio spec
    core->audio_spec.sample_rate = 44100;
    core->audio_spec.channels = 2;

    // Allocate audio float buffer
    core->audio_buffer_size = 4096;
    core->audio_float_buffer
        = (float*)arena_alloc_raw(arena, core->audio_buffer_size * 2 * sizeof(float), alignof(float));

    fl_log_info("ScummVM core created");

    return core;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destroy core instance

static void scummvm_destroy(void* core_instance) {
    if (!core_instance)
        return;

    ScummVMCore* core = (ScummVMCore*)core_instance;

    fl_log_info("Destroying ScummVM core");

    if (core->osystem) {
        core->osystem->quit();
        delete core->osystem;
        core->osystem = nullptr;
        g_system = nullptr;
    }

    // Core and audio buffer memory is arena-allocated and freed with the arena.
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper: Read game ID from .scummvm file

static bool read_scummvm_file(const char* path, char* game_id, size_t game_id_size) {
    FILE* f = fopen(path, "r");
    if (!f) {
        return false;
    }

    // Read the first line (game ID)
    if (fgets(game_id, (int)game_id_size, f) == nullptr) {
        fclose(f);
        return false;
    }
    fclose(f);

    // Strip trailing whitespace/newlines
    size_t len = strlen(game_id);
    while (len > 0
           && (game_id[len - 1] == '\n' || game_id[len - 1] == '\r' || game_id[len - 1] == ' '
               || game_id[len - 1] == '\t')) {
        game_id[--len] = '\0';
    }

    return len > 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper: Get directory from path

static void get_directory(const char* path, char* dir, size_t dir_size) {
    strncpy(dir, path, dir_size - 1);
    dir[dir_size - 1] = '\0';

    // Find last slash
    char* last_slash = strrchr(dir, '/');
    if (!last_slash) {
        last_slash = strrchr(dir, '\\');
    }

    if (last_slash) {
        *last_slash = '\0';
    } else {
        // No directory component, use current dir
        strcpy(dir, ".");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mount media (game directory or .scummvm file)

static bool scummvm_mount_media(void* core_instance, const char* path) {
    ScummVMCore* core = (ScummVMCore*)core_instance;

    if (!core || !core->osystem) {
        fprintf(stderr, "[ScummVM ERROR] Invalid core instance\n");
        return false;
    }

    SCUMMVM_LOG(core, "Mounting game: %s", path);

    // Check if this is a .scummvm file
    const char* ext = strrchr(path, '.');
    if (ext && strcasecmp(ext, ".scummvm") == 0) {
        // Read game ID from file
        if (!read_scummvm_file(path, core->game_id, sizeof(core->game_id))) {
            SCUMMVM_ERROR(core, "Failed to read game ID from %s", path);
            return false;
        }

        // Get the directory containing the .scummvm file
        get_directory(path, core->game_path, sizeof(core->game_path));

        SCUMMVM_LOG(core, "Game ID: %s, Path: %s", core->game_id, core->game_path);
    } else {
        // Assume path is a directory - would need auto-detection
        SCUMMVM_ERROR(core, "Direct directory launching not yet supported. Use a .scummvm file.");
        return false;
    }

    // Set game info on OSystem (coroutines initialized on first run_frame)
    core->osystem->setGamePath(core->game_path);
    core->osystem->setGameId(core->game_id);

    core->state = RpEmuLifecycleState_Running;

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Unmount current media

static void scummvm_unmount_media(void* core_instance) {
    ScummVMCore* core = (ScummVMCore*)core_instance;

    if (!core || !core->osystem)
        return;

    SCUMMVM_LOG(core, "Unmounting game");
    core->game_path[0] = '\0';
    core->state = RpEmuLifecycleState_Idle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Map SDL-style scan codes to ScummVM KeyCode
// This covers the most common keys used in adventure games

static Common::KeyCode scancode_to_keycode(uint8_t scancode) {
    // SDL scancodes mapping (subset for common keys)
    switch (scancode) {
        case 4:
            return Common::KEYCODE_a;
        case 5:
            return Common::KEYCODE_b;
        case 6:
            return Common::KEYCODE_c;
        case 7:
            return Common::KEYCODE_d;
        case 8:
            return Common::KEYCODE_e;
        case 9:
            return Common::KEYCODE_f;
        case 10:
            return Common::KEYCODE_g;
        case 11:
            return Common::KEYCODE_h;
        case 12:
            return Common::KEYCODE_i;
        case 13:
            return Common::KEYCODE_j;
        case 14:
            return Common::KEYCODE_k;
        case 15:
            return Common::KEYCODE_l;
        case 16:
            return Common::KEYCODE_m;
        case 17:
            return Common::KEYCODE_n;
        case 18:
            return Common::KEYCODE_o;
        case 19:
            return Common::KEYCODE_p;
        case 20:
            return Common::KEYCODE_q;
        case 21:
            return Common::KEYCODE_r;
        case 22:
            return Common::KEYCODE_s;
        case 23:
            return Common::KEYCODE_t;
        case 24:
            return Common::KEYCODE_u;
        case 25:
            return Common::KEYCODE_v;
        case 26:
            return Common::KEYCODE_w;
        case 27:
            return Common::KEYCODE_x;
        case 28:
            return Common::KEYCODE_y;
        case 29:
            return Common::KEYCODE_z;
        case 30:
            return Common::KEYCODE_1;
        case 31:
            return Common::KEYCODE_2;
        case 32:
            return Common::KEYCODE_3;
        case 33:
            return Common::KEYCODE_4;
        case 34:
            return Common::KEYCODE_5;
        case 35:
            return Common::KEYCODE_6;
        case 36:
            return Common::KEYCODE_7;
        case 37:
            return Common::KEYCODE_8;
        case 38:
            return Common::KEYCODE_9;
        case 39:
            return Common::KEYCODE_0;
        case 40:
            return Common::KEYCODE_RETURN;
        case 41:
            return Common::KEYCODE_ESCAPE;
        case 42:
            return Common::KEYCODE_BACKSPACE;
        case 43:
            return Common::KEYCODE_TAB;
        case 44:
            return Common::KEYCODE_SPACE;
        case 79:
            return Common::KEYCODE_RIGHT;
        case 80:
            return Common::KEYCODE_LEFT;
        case 81:
            return Common::KEYCODE_DOWN;
        case 82:
            return Common::KEYCODE_UP;
        case 58:
            return Common::KEYCODE_F1;
        case 59:
            return Common::KEYCODE_F2;
        case 60:
            return Common::KEYCODE_F3;
        case 61:
            return Common::KEYCODE_F4;
        case 62:
            return Common::KEYCODE_F5;
        case 63:
            return Common::KEYCODE_F6;
        case 64:
            return Common::KEYCODE_F7;
        case 65:
            return Common::KEYCODE_F8;
        case 66:
            return Common::KEYCODE_F9;
        case 67:
            return Common::KEYCODE_F10;
        case 68:
            return Common::KEYCODE_F11;
        case 69:
            return Common::KEYCODE_F12;
        default:
            return Common::KEYCODE_INVALID;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process input from frontend and generate ScummVM events

static void process_input(ScummVMCore* core, const RpEmuInputState* input) {
    if (!input || !core->osystem)
        return;

    // Get screen dimensions for mouse coordinate clamping
    ReplayGraphicsManager* gfx = core->osystem->getReplayGraphicsManager();
    int screen_width = gfx ? gfx->getFrameWidth() : 320;
    int screen_height = gfx ? gfx->getFrameHeight() : 200;

    // Process mouse movement
    if (input->mouse_x != 0 || input->mouse_y != 0) {
        core->mouse_x += input->mouse_x;
        core->mouse_y += input->mouse_y;

        // Clamp to screen bounds
        if (core->mouse_x < 0)
            core->mouse_x = 0;
        if (core->mouse_y < 0)
            core->mouse_y = 0;
        if (core->mouse_x >= screen_width)
            core->mouse_x = screen_width - 1;
        if (core->mouse_y >= screen_height)
            core->mouse_y = screen_height - 1;

        Common::Event event;
        event.type = Common::EVENT_MOUSEMOVE;
        event.mouse.x = core->mouse_x;
        event.mouse.y = core->mouse_y;
        core->osystem->queueEvent(event);
    }

    // Process mouse left button
    if (input->mouse_left != core->prev_input.mouse_left) {
        Common::Event event;
        event.type = input->mouse_left ? Common::EVENT_LBUTTONDOWN : Common::EVENT_LBUTTONUP;
        event.mouse.x = core->mouse_x;
        event.mouse.y = core->mouse_y;
        core->osystem->queueEvent(event);
    }

    // Process mouse right button
    if (input->mouse_right != core->prev_input.mouse_right) {
        Common::Event event;
        event.type = input->mouse_right ? Common::EVENT_RBUTTONDOWN : Common::EVENT_RBUTTONUP;
        event.mouse.x = core->mouse_x;
        event.mouse.y = core->mouse_y;
        core->osystem->queueEvent(event);
    }

    // Process keyboard keys
    for (int i = 0; i < 256; i++) {
        if (input->keyboard_keys[i] != core->prev_input.keyboard_keys[i]) {
            Common::KeyCode keycode = scancode_to_keycode((uint8_t)i);
            if (keycode != Common::KEYCODE_INVALID) {
                Common::Event event;
                event.type = input->keyboard_keys[i] ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
                event.kbdRepeat = false;
                event.kbd.keycode = keycode;
                event.kbd.ascii = (keycode < 128) ? keycode : 0;
                event.kbd.flags = 0;
                core->osystem->queueEvent(event);
            }
        }
    }

    // Store current input as previous for next frame
    core->prev_input = *input;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Run one frame of emulation

static void scummvm_run_frame(void* core_instance, RpEmuFrameContext* ctx) {
    ScummVMCore* core = (ScummVMCore*)core_instance;

    if (!core || !core->osystem || core->state != RpEmuLifecycleState_Running) {
        return;
    }

    // Initialize coroutines on first run_frame (must be on core thread for TLS)
    if (!core->coroutines_initialized) {
        SCUMMVM_LOG(core, "Initializing ScummVM coroutines on core thread");
        core->osystem->initCoroutines();
        core->coroutines_initialized = true;
        SCUMMVM_LOG(core, "Coroutines initialized successfully");
    }

    // Check if ScummVM has quit
    if (core->osystem->hasQuit()) {
        core->state = RpEmuLifecycleState_Idle;
        return;
    }

    // Process input from frontend
    process_input(core, ctx->input);

    // Run one frame
    core->osystem->runFrame();

    // Get video buffer from graphics manager
    ReplayGraphicsManager* gfx = core->osystem->getReplayGraphicsManager();
    if (gfx) {
        ctx->video_buffer = gfx->getFrameBuffer();
        ctx->video_pitch = gfx->getFramePitch();

        // Update video spec if resolution changed
        if (gfx->getFrameWidth() != core->video_spec.width || gfx->getFrameHeight() != core->video_spec.height) {
            core->video_spec.width = gfx->getFrameWidth();
            core->video_spec.height = gfx->getFrameHeight();
            core->video_spec.visible_width = gfx->getFrameWidth();
            core->video_spec.visible_height = gfx->getFrameHeight();
        }
    }

    // Get audio from mixer manager
    // Note: Audio is mixed during pollEvent() inside the ScummVM coroutine,
    // so we just retrieve the samples that were already mixed
    ReplayMixerManager* mixer = core->osystem->getReplayMixerManager();
    if (mixer && core->audio_float_buffer) {
        uint32_t samples = mixer->getAudioSamples();

        if (samples > 0) {
            mixer->convertToFloat(core->audio_float_buffer, samples);
            ctx->audio_buffer = core->audio_float_buffer;
            ctx->audio_frames = samples;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Reset

static void scummvm_reset(void* core_instance) {
    ScummVMCore* core = (ScummVMCore*)core_instance;

    if (!core || !core->osystem)
        return;

    SCUMMVM_LOG(core, "Reset (not supported in ScummVM)");
    // ScummVM doesn't have a reset concept - games manage their own state
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hard reset

static void scummvm_hard_reset(void* core_instance) {
    scummvm_reset(core_instance);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get video spec

static void scummvm_get_video_spec(void* core_instance, RpVideoSpec* spec) {
    ScummVMCore* core = (ScummVMCore*)core_instance;

    if (core) {
        *spec = core->video_spec;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get audio spec

static void scummvm_get_audio_spec(void* core_instance, RpAudioSpec* spec) {
    ScummVMCore* core = (ScummVMCore*)core_instance;

    if (core) {
        *spec = core->audio_spec;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get lifecycle state

static RpEmuLifecycleState scummvm_get_state(void* core_instance) {
    ScummVMCore* core = (ScummVMCore*)core_instance;

    if (!core) {
        return RpEmuLifecycleState_Idle;
    }

    return core->state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialize state (save state) - Not supported by ScummVM

static size_t scummvm_serialize(void* core_instance, void* buffer, size_t buffer_size) {
    UNUSED(core_instance);
    UNUSED(buffer);
    UNUSED(buffer_size);
    // ScummVM has its own save system, we don't support save states
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Deserialize state (load state) - Not supported by ScummVM

static bool scummvm_deserialize(void* core_instance, const void* buffer, size_t buffer_size) {
    UNUSED(core_instance);
    UNUSED(buffer);
    UNUSED(buffer_size);
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core API table

static RpEmuAPI s_scummvm_api = {
    .get_info = scummvm_get_info,
    .create = scummvm_create,
    .destroy = scummvm_destroy,
    .mount_media = scummvm_mount_media,
    .unmount_media = scummvm_unmount_media,
    .run_frame = scummvm_run_frame,
    .reset = scummvm_reset,
    .hard_reset = scummvm_hard_reset,
    .get_video_spec = scummvm_get_video_spec,
    .get_audio_spec = scummvm_get_audio_spec,
    .get_state = scummvm_get_state,
    .copy_audio = nullptr,
    .serialize = scummvm_serialize,
    .deserialize = scummvm_deserialize,
    .set_config_bitmask = nullptr,
    .set_config_int = nullptr,
    .set_config_float = nullptr,
    .set_config_bool = nullptr,
    .set_config_string = nullptr,
    .get_config_string = nullptr,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Plugin entry point

extern "C" RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void) {
    return &s_scummvm_api;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ABI version export. The host resolves this before it reads the vtable above and refuses the
// plugin when the answer is not the version it was built for.

RP_EMU_PLUGIN_ABI_VERSION_EXPORT()

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
