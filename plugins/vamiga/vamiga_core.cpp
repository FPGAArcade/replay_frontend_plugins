///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// vAmiga Core Plugin - Wrapper around vAmiga emulator
//
// This plugin wraps the vAmiga C++ library into the Replay Core Plugin API.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <flowi/arena/arena.h>
#include "replay/emu_plugin.h"
#include <flowi/core/log_macros.h>
#include "replay/plugin_info.h"
#include <flowi/core/log_macros.h>
#include <flowi/string/string.h>
// flowi's platform.h, pulled in by the headers above, defines likely and unlikely, and vAmiga's
// Macros.h defines its own. Dropping the host's spellings here lets vAmiga's headers install theirs
// without a redefinition, and nothing below this point uses the host's.
#undef likely
#undef unlikely

#include "VAmiga.h"
#include "Components/Amiga.h"
#include "Components/CPU/CPU.h"
#include "Components/Memory/Memory.h"
#include "hle_rom.h"

#include <cstring>
#include <memory>
#include <string>

using namespace vamiga;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Result struct for option lookup

struct OptFindResult {
    Opt opt;
    bool found;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper to find Opt enum from key string (e.g., "CPU.OVERCLOCKING" -> Opt::CPU_OVERCLOCKING)

static OptFindResult find_opt_by_key(FlString key) {
    // Iterate through all valid Opt values and compare keys
    for (long i = OptEnum::minVal; i <= OptEnum::maxVal; i++) {
        Opt opt = static_cast<Opt>(i);
        const char* opt_key = OptEnum::fullKey(opt);
        if (opt_key && string_equals(key, string_from_cstr(opt_key))) {
            return { opt, true };
        }
    }
    return { {}, false };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core instance - wraps VAmiga emulator

struct VAmigaCore {
    FlArena* arena;
    std::unique_ptr<VAmiga> vamiga;
    volatile bool shutting_down; // Set before halt to prevent HLE callback races (zero-init by arena)
    RpEmuLifecycleState state;

    // Cached pointers for HLE bridge (avoids accessing vamiga unique_ptr from emulator thread)
    CPU* hle_cpu;
    Memory* hle_mem;

    // Video buffer
    const uint32_t* video_buffer; // Points to vAmiga's texture

    // Audio buffer (interleaved stereo float samples)
    // Sized for slightly more than one frame at 48kHz/50fps = 960 samples
    static constexpr size_t AUDIO_BUFFER_SIZE = 2048;
    float audio_buffer[AUDIO_BUFFER_SIZE * 2]; // *2 for stereo interleaved
    size_t audio_sample_count;

    // Warp control: once audio DMA is detected, disable warp permanently
    bool warp_disabled;

    // Specs
    RpVideoSpec video_spec;
    RpAudioSpec audio_spec;

    // Current disk paths (for get_config_string)
    FlString disk_paths[4]; // DF0-DF3

    // Multi-disk auto-swap state
    FlString* media_list; // Disk paths (arena-allocated)
    u32 media_count;
    u32 current_disk_index;
    bool auto_swap_enabled;
    bool initial_insert_seen; // Skip first DISK_INSERT (initial boot disk)

    // HLE ROM state (active when no Kickstart ROM found)
    bool hle_mode;
    HleAmigaState hle_state;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get core information

static void vamiga_get_info(RpEmuInfo* info) {
    info->emu_name = "vAmiga";
    info->emu_version = "4.4";
    info->system_name = "Commodore Amiga";
    info->supported_extensions = "adf|adz|dms|hdf|hdz";
    info->requires_bios = false; // HLE ROM fallback when no Kickstart ROM available
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HLE ROM bridge functions - translate between vAmiga's Memory class and HleAmigaState

static void hle_poke8_bridge(void* ctx, uint32_t addr, uint8_t val) {
    auto* mem = static_cast<vamiga::Memory*>(ctx);
    mem->poke8<vamiga::Accessor::CPU>(addr, val);
}

static void hle_poke16_bridge(void* ctx, uint32_t addr, uint16_t val) {
    auto* mem = static_cast<vamiga::Memory*>(ctx);
    mem->poke16<vamiga::Accessor::CPU>(addr, val);
}

static uint8_t hle_peek8_bridge(void* ctx, uint32_t addr) {
    auto* mem = static_cast<vamiga::Memory*>(ctx);
    return mem->peek8<vamiga::Accessor::CPU>(addr);
}

static uint16_t hle_peek16_bridge(void* ctx, uint32_t addr) {
    auto* mem = static_cast<vamiga::Memory*>(ctx);
    return mem->peek16<vamiga::Accessor::CPU>(addr);
}

static bool hle_bridge(void* ctx, uint32_t pc) {
    VAmigaCore* core = static_cast<VAmigaCore*>(ctx);
    if (!core || core->shutting_down)
        return false;

    // Use cached pointers — these are set once during create and never go through
    // the unique_ptr, avoiding a race with reset() during shutdown.
    CPU* cpu = core->hle_cpu;
    Memory* mem = core->hle_mem;

    // Populate HleAmigaState from CPU/Memory state
    HleAmigaState* state = &core->hle_state;
    for (int i = 0; i < 8; i++) {
        state->d[i] = cpu->getD(i);
        state->a[i] = cpu->getA(i);
    }
    state->pc = cpu->getPC();
    state->sr = cpu->getSR();
    state->chip_ram = mem->chip;
    state->chip_ram_size = mem->getConfig().chipSize;
    state->rom = mem->rom;
    state->rom_size = mem->getConfig().romSize;
    state->poke8 = hle_poke8_bridge;
    state->poke16 = hle_poke16_bridge;
    state->peek8 = hle_peek8_bridge;
    state->peek16 = hle_peek16_bridge;
    state->mem_ctx = mem;

    bool handled = hle_rom_dispatch(state, pc);

    if (handled) {
        for (int i = 0; i < 8; i++) {
            cpu->setD(i, state->d[i]);
            cpu->setA(i, state->a[i]);
        }
        cpu->setPC(state->pc);
        cpu->setSR(state->sr);
    }
    return handled;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create core instance

static void* vamiga_create(FlArena* arena) {

    fl_log_info("Creating core");

    VAmigaCore* core = (VAmigaCore*)arena_alloc_raw_zero(arena, sizeof(VAmigaCore), alignof(VAmigaCore));
    core->arena = arena;
    core->state = RpEmuLifecycleState_Idle;

    try {
        // Create vAmiga instance
        core->vamiga = std::make_unique<VAmiga>();

        // Configure as standard Amiga 500: OCS, 512KB chip + 512KB slow (trapdoor)
        // No fast RAM - many demos are incompatible with fast RAM
        core->vamiga->set(ConfigScheme::A500_OCS_1MB);

        // Set up video spec (vAmiga native resolution)
        // Full buffer includes blanking areas
        core->video_spec.width = HPIXELS;  // 912 pixels
        core->video_spec.height = VPIXELS; // 313 pixels
        core->video_spec.pixel_format = RpPixelFormat_Xrgb8888;
        core->video_spec.fps = 50.0f; // PAL

        // Visible area excludes HBLANK and VBLANK regions
        // Based on vAmiga's TextureRect.swift largestVisible calculation:
        // x1 = 4 * HBLANK_CNT = 4 * 0x12 = 72 pixels
        // x2 = 4 * PAL::HPOS_CNT = 4 * 227 = 908 pixels
        // y1 = PAL::VBLANK_CNT = 0x1A = 26 lines
        // y2 = PAL::VPOS_CNT = 313 lines
        // width = x2 - x1 = 836, height = y2 - y1 - 1 = 286
        constexpr uint32_t HBLANK_PIXELS = 4 * 0x12;  // 72 pixels
        constexpr uint32_t PAL_HPOS_PIXELS = 4 * 227; // 908 pixels (PAL horizontal extent)
        constexpr uint32_t VBLANK_LINES = 0x1A;       // 26 lines (PAL)
        core->video_spec.visible_x = HBLANK_PIXELS;
        core->video_spec.visible_y = VBLANK_LINES;
        core->video_spec.visible_width = PAL_HPOS_PIXELS - HBLANK_PIXELS; // 836 pixels
        core->video_spec.visible_height = VPIXELS - VBLANK_LINES - 1;     // 286 lines

        // Set up audio spec (48kHz stereo to match SDL)
        core->audio_spec.sample_rate = 48000;
        core->audio_spec.channels = 2;
        core->audio_sample_count = 0;

        // Launch the emulator thread early so shutdown() works even if no game is loaded
        // This must happen before any powerOn/powerOff calls
        core->vamiga->launch(nullptr, nullptr);

        // Set host sample rate to 48kHz to match SDL
        core->vamiga->set(Opt::HOST_SAMPLE_RATE, 48000);

        // Set audio volumes to 100% (default may be 0)
        core->vamiga->set(Opt::AUD_VOL0, 100);
        core->vamiga->set(Opt::AUD_VOL1, 100);
        core->vamiga->set(Opt::AUD_VOL2, 100);
        core->vamiga->set(Opt::AUD_VOL3, 100);
        core->vamiga->set(Opt::AUD_VOLL, 100);
        core->vamiga->set(Opt::AUD_VOLR, 100);

        // Center all channels for mono output (no hard stereo separation)
        core->vamiga->set(Opt::AUD_PAN0, 200);
        core->vamiga->set(Opt::AUD_PAN1, 200);
        core->vamiga->set(Opt::AUD_PAN2, 200);
        core->vamiga->set(Opt::AUD_PAN3, 200);

        // Enable ASR (Adaptive Sample Rate) for smoother audio
        core->vamiga->set(Opt::AUD_ASR, 1);

        // Enable warp mode during disk activity for faster loading
        // Warp::AUTO (0) = warp when disk is spinning
        core->vamiga->set(Opt::AMIGA_WARP_MODE, 0); // Warp::AUTO
        // Warp for first 3 seconds after boot
        core->vamiga->set(Opt::AMIGA_WARP_BOOT, 3);

        // Try to load Kickstart ROM from various locations
        bool rom_loaded = false;

        // Build paths using HOME environment variable
        const char* home = getenv("HOME");
        char rom_path1[512] = { 0 };
        char rom_path2[512] = { 0 };
        if (home) {
            snprintf(rom_path1, sizeof(rom_path1), "%s/.replay2/system/roms/kick13.rom", home);
            snprintf(rom_path2, sizeof(rom_path2), "%s/.replay2/system/cores/vAmiga/kick13.rom", home);
        }

        const char* rom_paths[] = { "test_data/roms/kick13.rom", // Development
                                    rom_path1,                   // User ROMs (~/.replay2/system/roms/)
                                    rom_path2,                   // Core directory (~/.replay2/system/cores/vAmiga/)
                                    nullptr };

        for (int i = 0; rom_paths[i] != nullptr && rom_paths[i][0] != '\0'; i++) {
            try {
                fl_log_debug("Trying ROM path: %s", rom_paths[i]);
                core->vamiga->mem.loadRom(rom_paths[i]);
                fl_log_info("Loaded Kickstart ROM: %s", rom_paths[i]);
                rom_loaded = true;
                break;
            } catch (const std::exception& e) {
                fl_log_debug("ROM load failed for %s: %s", rom_paths[i], e.what());
            } catch (...) {
                fl_log_debug("ROM load failed for %s: unknown error", rom_paths[i]);
            }
        }

        if (!rom_loaded) {
            fl_log_info("No Kickstart ROM found, enabling HLE ROM mode");

            // Allocate ROM buffer and initialize HLE ROM
            Memory* mem = core->vamiga->mem.mem;
            mem->allocRom(512 * 1024);

            HleAmigaState* hle = &core->hle_state;
            *hle = {};
            hle->rom = mem->rom;
            hle->rom_size = 512 * 1024;
            hle->chip_ram = mem->chip;
            hle->chip_ram_size = mem->getConfig().chipSize;

            // Populate fast RAM info from vAmiga's actual memory config
            const auto& mem_config = mem->getConfig();
            Amiga* amiga = core->vamiga->amiga.amiga;
            if (mem_config.fastSize > 0 && mem->fast) {
                hle->fast_ram = mem->fast;
                hle->fast_ram_size = mem_config.fastSize;
                hle->fast_ram_base = amiga->ramExpansion.getBaseAddr();
            }
            if (mem_config.slowSize > 0 && mem->slow) {
                hle->slow_ram = mem->slow;
                hle->slow_ram_size = mem_config.slowSize;
            }

            hle_rom_init(hle);

            // Install HLE callback on the CPU and cache pointers for thread-safe access
            CPU* cpu = core->vamiga->cpu.cpu;
            cpu->hleCallback = hle_bridge;
            cpu->hleContext = core;
            core->hle_cpu = cpu;
            core->hle_mem = mem;
            core->hle_mode = true;
            rom_loaded = true;
        }

        // Power on and start the emulator
        if (rom_loaded) {
            core->vamiga->powerOn();
            core->vamiga->run();
            core->state = RpEmuLifecycleState_Running;
            fl_log_info("Core created and running%s", core->hle_mode ? " (HLE ROM)" : "");
        } else {
            core->state = RpEmuLifecycleState_Idle;
            fl_log_info("Core created (waiting for ROM)");
        }

    } catch (const std::exception& e) {
        fl_log_error("Core creation failed: %s", e.what());
        core->state = RpEmuLifecycleState_Error;
        return nullptr;
    }

    return core;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destroy core instance

static void vamiga_destroy(void* core_instance) {
    if (!core_instance)
        return;

    VAmigaCore* core = (VAmigaCore*)core_instance;
    fl_log_info("Destroying core");

    if (core->vamiga) {
        // Signal shutdown so hle_bridge returns early if called mid-frame
        core->shutting_down = true;

        // halt() terminates the emulator thread synchronously (sends HALT + joins).
        core->vamiga->halt();
        core->vamiga.reset();
    }

    core->state = RpEmuLifecycleState_Idle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper to check file extension (case-insensitive)

static bool has_extension(const char* path, const char* ext) {
    if (!path || !ext)
        return false;

    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);

    if (path_len < ext_len + 1)
        return false;

    const char* file_ext = path + path_len - ext_len;
    if (*(file_ext - 1) != '.')
        return false;

    // Case-insensitive comparison
    for (size_t i = 0; i < ext_len; i++) {
        char c1 = file_ext[i];
        char c2 = ext[i];
        if (c1 >= 'A' && c1 <= 'Z')
            c1 = c1 - 'A' + 'a';
        if (c2 >= 'A' && c2 <= 'Z')
            c2 = c2 - 'A' + 'a';
        if (c1 != c2)
            return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mount media (disk image, ROM, etc.)

static bool vamiga_mount_media(void* core_instance, const char* path) {
    VAmigaCore* core = (VAmigaCore*)core_instance;

    if (!core || !core->vamiga) {
        fl_log_error("Invalid core instance");
        return false;
    }

    fl_log_info("Mounting media: %s", path);

    try {
        // Reset multi-disk auto-swap state for a fresh mount.
        core->vamiga->df0.clearAutoSwap();
        core->media_list = nullptr;
        core->media_count = 0;
        core->current_disk_index = 0;
        core->initial_insert_seen = false;
        core->auto_swap_enabled = false;

        if (path && *path) {
            // Check file extension to determine media type
            if (has_extension(path, "hdf") || has_extension(path, "hdz")) {
                // Hard disk image - mount as HD0
                // First enable the hard drive controller
                core->vamiga->set(Opt::HDC_CONNECT, 0, true);
                core->vamiga->hd0.attach(path);
                fl_log_info("Mounted hard disk as HD0");
            } else {
                // Floppy disk image (ADF, ADZ, DMS, etc.) - insert in DF0
                core->vamiga->df0.insert(path, false); // false = not write-protected
                core->disk_paths[0] = string_copy(core->arena, string_from_cstr(path));
                fl_log_info("Inserted disk in DF0: %s", path);
            }
        }

        return true;

    } catch (const std::exception& e) {
        fl_log_error("Failed to mount media: %s", e.what());
        return false;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Unmount current media

static void vamiga_unmount_media(void* core_instance) {
    VAmigaCore* core = (VAmigaCore*)core_instance;

    if (!core || !core->vamiga)
        return;

    fl_log_info("Unmounting media");

    try {
        core->vamiga->pause();
        core->vamiga->df0.clearAutoSwap();
        core->vamiga->df0.ejectDisk();
        core->media_list = nullptr;
        core->media_count = 0;
        core->current_disk_index = 0;
        core->initial_insert_seen = false;
        core->auto_swap_enabled = false;
        core->state = RpEmuLifecycleState_Idle;
    } catch (const std::exception& e) {
        fl_log_error("Failed to unmount media: %s", e.what());
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Queue the next auto-swap disk (if any remain)

static void queue_next_auto_swap(VAmigaCore* core) {
    u32 next = core->current_disk_index + 1;
    if (next < core->media_count) {
        FlString next_path = core->media_list[next];
        std::string path(next_path.data, next_path.length);

        core->vamiga->df0.setAutoSwapDisk(path);
        fl_log_info("Auto-swap: queued disk %u/%u: %s", next + 1, core->media_count, path.c_str());
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mount multiple media files (multi-disk support)

static bool vamiga_mount_media_list(void* core_instance, const FlString* paths, uint32_t count) {
    VAmigaCore* core = (VAmigaCore*)core_instance;

    if (!core || !core->vamiga || !paths || count == 0) {
        fl_log_error("Invalid parameters for mount_media_list");
        return false;
    }

    fl_log_info("Mounting media list: %u disk(s), auto_swap=%s", count, core->auto_swap_enabled ? "true" : "false");

    // Reset multi-disk state before loading a new list.
    core->vamiga->df0.clearAutoSwap();
    core->media_list = nullptr;
    core->media_count = 0;
    core->current_disk_index = 0;
    core->initial_insert_seen = false;
    core->auto_swap_enabled = false;

    // Store paths in arena
    core->media_list = (FlString*)arena_alloc_raw(core->arena, count * sizeof(FlString), alignof(FlString));
    for (u32 i = 0; i < count; i++) {
        core->media_list[i] = string_copy(core->arena, paths[i]);
    }
    core->media_count = count;
    core->current_disk_index = 0;

    // Insert first disk in DF0 (all disks go through DF0 via auto-swap)
    std::string first_path(paths[0].data, paths[0].length);

    try {
        core->vamiga->df0.insert(first_path, false);
        core->disk_paths[0] = core->media_list[0];
        fl_log_info("Inserted disk 1/%u in DF0: %s", count, first_path.c_str());
    } catch (const std::exception& e) {
        fl_log_error("Failed to insert first disk: %s", e.what());
        return false;
    }

    // If auto-swap enabled and we have more disks, queue the next one
    if (core->auto_swap_enabled && count > 1) {
        queue_next_auto_swap(core);
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Run one frame of emulation

static void vamiga_run_frame(void* core_instance, RpEmuFrameContext* ctx) {
    VAmigaCore* core = (VAmigaCore*)core_instance;

    if (!core || !core->vamiga || core->state != RpEmuLifecycleState_Running) {
        return;
    }

    try {
        // TODO: Apply input from ctx->input
        // - Map buttons to joystick
        // - Map mouse deltas
        // - Map keyboard

        // Wake up emulator (runs one frame)
        core->vamiga->wakeUp();

        // Drain message queue for auto-swap handling
        if (core->auto_swap_enabled && core->media_count > 1) {
            vamiga::Message msg;
            while (core->vamiga->msgQueue.getMsg(msg)) {
                if (msg.type == vamiga::Msg::DISK_INSERT && msg.drive.nr == 0) {
                    // Skip the initial disk insertion (boot disk)
                    if (!core->initial_insert_seen) {
                        core->initial_insert_seen = true;
                        continue;
                    }

                    // A swap happened - advance index and queue next
                    core->current_disk_index++;
                    fl_log_info("Auto-swap: disk %u/%u inserted in DF0", core->current_disk_index + 1,
                                core->media_count);

                    if (core->current_disk_index + 1 < core->media_count) {
                        queue_next_auto_swap(core);
                    }
                } else if (msg.type == vamiga::Msg::DRIVE_POLL && msg.drive.nr == 0) {
                    fl_log_debug("Auto-swap: DF0 polling detected (cyl %d)", msg.drive.value);
                }
            }
        }

        // Get video buffer
        core->vamiga->videoPort.lockTexture();
        core->video_buffer = core->vamiga->videoPort.getTexture();

        ctx->video_buffer = core->video_buffer;
        ctx->video_pitch = HPIXELS * sizeof(uint32_t); // 912 * 4 bytes

        // Don't unlock yet - frontend will read from this buffer
        // We'll unlock at the start of next frame
        if (ctx->video_buffer) {
            core->vamiga->videoPort.unlockTexture();
        }

        // Capture audio AFTER frame completes (video lock ensures frame is done)
        // Limit to ~1 frame worth (48000/50 = 960) to avoid buffer buildup
        isize max_samples = 960;
        isize copied = core->vamiga->audioPort.copyInterleaved(core->audio_buffer, max_samples);
        core->audio_sample_count = static_cast<size_t>(copied);

        // Check if audio DMA is active by reading DMACON register
        // This works even during warp mode (when audio output is muted)
        // DMACON bits: DMAEN (bit 9) must be set, plus any of AUD0-3 (bits 0-3)
        if (!core->warp_disabled) {
            auto agnus_info = core->vamiga->agnus.getInfo();
            u16 dmacon = agnus_info.dmacon;
            bool dma_master_enabled = (dmacon & 0x200) != 0; // DMAEN bit 9
            bool audio_dma_enabled = (dmacon & 0x0F) != 0;   // AUD0-AUD3 bits 0-3

            if (dma_master_enabled && audio_dma_enabled) {
                core->vamiga->set(Opt::AMIGA_WARP_MODE, 1); // Warp::NEVER
                core->warp_disabled = true;
                fl_log_info("Audio DMA detected (DMACON=0x%04X), warp mode disabled", dmacon);
            }
        }

        // Set audio output in frame context
        if (core->audio_sample_count > 0) {
            ctx->audio_buffer = core->audio_buffer;
            ctx->audio_frames = static_cast<uint32_t>(core->audio_sample_count);
        } else {
            ctx->audio_buffer = nullptr;
            ctx->audio_frames = 0;
        }

    } catch (const std::exception& e) {
        fl_log_error("Frame execution failed: %s", e.what());
        core->state = RpEmuLifecycleState_Error;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Reset

static void vamiga_reset(void* core_instance) {
    VAmigaCore* core = (VAmigaCore*)core_instance;

    if (!core || !core->vamiga)
        return;

    fl_log_info("Soft reset");
    // Note: vAmiga's softReset() has a bug where Paula asserts cpu.getIPL() == 0,
    // but CPU::_didReset() only resets Moira (and IPL) on hard reset.
    // Using hardReset() instead until vAmiga fixes this.
    core->vamiga->suspend();
    core->vamiga->hardReset();
    core->vamiga->resume();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hard reset

static void vamiga_hard_reset(void* core_instance) {
    VAmigaCore* core = (VAmigaCore*)core_instance;

    if (!core || !core->vamiga)
        return;

    fl_log_info("Hard reset");
    // Suspend emulator thread to ensure safe reset (prevents IPL assertion in Paula)
    core->vamiga->suspend();
    core->vamiga->hardReset();
    core->vamiga->resume();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get video spec

static void vamiga_get_video_spec(void* core_instance, RpVideoSpec* spec) {
    VAmigaCore* core = (VAmigaCore*)core_instance;

    if (core) {
        *spec = core->video_spec;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get audio spec

static void vamiga_get_audio_spec(void* core_instance, RpAudioSpec* spec) {
    VAmigaCore* core = (VAmigaCore*)core_instance;

    if (core) {
        *spec = core->audio_spec;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copy audio samples

static uint32_t vamiga_copy_audio(void* core_instance, float* buffer, uint32_t max_samples) {
    VAmigaCore* core = (VAmigaCore*)core_instance;

    if (!core || !buffer || max_samples == 0) {
        return 0;
    }

    // Return the samples captured during run_frame
    uint32_t samples_to_copy = static_cast<uint32_t>(core->audio_sample_count);
    if (samples_to_copy > max_samples) {
        samples_to_copy = max_samples;
    }

    // Copy interleaved stereo samples (2 floats per sample)
    memcpy(buffer, core->audio_buffer, samples_to_copy * 2 * sizeof(float));

    return samples_to_copy;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get lifecycle state

static RpEmuLifecycleState vamiga_get_state(void* core_instance) {
    VAmigaCore* core = (VAmigaCore*)core_instance;

    if (!core) {
        return RpEmuLifecycleState_Idle;
    }

    return core->state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialize state (save state)

static size_t vamiga_serialize(void* core_instance, void* buffer, size_t buffer_size) {
    VAmigaCore* core = (VAmigaCore*)core_instance;

    if (!core || !core->vamiga) {
        return 0;
    }

    // TODO: Implement using vAmiga's snapshot system
    // For now, return 0 (not implemented)
    UNUSED(buffer);
    UNUSED(buffer_size);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Deserialize state (load state)

static bool vamiga_deserialize(void* core_instance, const void* buffer, size_t buffer_size) {
    VAmigaCore* core = (VAmigaCore*)core_instance;

    if (!core || !core->vamiga) {
        return false;
    }

    // TODO: Implement using vAmiga's snapshot system
    UNUSED(buffer);
    UNUSED(buffer_size);
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set integer configuration option

static void vamiga_set_config_int(void* core_instance, FlString key, int32_t value) {
    VAmigaCore* core = (VAmigaCore*)core_instance;
    if (!core || !core->vamiga) {
        return;
    }

    OptFindResult result = find_opt_by_key(key);
    if (!result.found) {
        fl_log_warning("Unknown config key for int: %.*s", (int)key.length, key.data);
        return;
    }

    try {
        core->vamiga->set(result.opt, static_cast<int64_t>(value));
        fl_log_debug("Set config %.*s = %d", (int)key.length, key.data, value);
    } catch (const std::exception& e) {
        fl_log_error("Failed to set config %.*s: %s", (int)key.length, key.data, e.what());
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set float configuration option

static void vamiga_set_config_float(void* core_instance, FlString key, float value) {
    VAmigaCore* core = (VAmigaCore*)core_instance;
    if (!core || !core->vamiga) {
        return;
    }

    // vAmiga doesn't have native float options - convert to integer where applicable
    OptFindResult result = find_opt_by_key(key);
    if (!result.found) {
        fl_log_warning("Unknown config key for float: %.*s", (int)key.length, key.data);
        return;
    }

    try {
        // For now, convert float to int - vAmiga uses integer config values
        core->vamiga->set(result.opt, static_cast<int64_t>(value));
        fl_log_debug("Set config %.*s = %f (as int)", (int)key.length, key.data, value);
    } catch (const std::exception& e) {
        fl_log_error("Failed to set config %.*s: %s", (int)key.length, key.data, e.what());
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set boolean configuration option

static void vamiga_set_config_bool(void* core_instance, FlString key, bool value) {
    VAmigaCore* core = (VAmigaCore*)core_instance;
    if (!core || !core->vamiga) {
        return;
    }

    // Handle wrapper-level config keys
    if (string_equals(key, string_from_cstr("AUTO_SWAP_DISKS"))) {
        core->auto_swap_enabled = value;
        fl_log_info("Auto-swap disks: %s", value ? "enabled" : "disabled");

        if (!value) {
            core->vamiga->df0.clearAutoSwap();
        }

        // If enabling after media list was already mounted, queue the next disk now
        if (value && core->media_count > 1 && core->current_disk_index + 1 < core->media_count) {
            queue_next_auto_swap(core);
        }
        return;
    }

    OptFindResult result = find_opt_by_key(key);
    if (!result.found) {
        fl_log_warning("Unknown config key for bool: %.*s", (int)key.length, key.data);
        return;
    }

    try {
        core->vamiga->set(result.opt, value ? 1 : 0);
        fl_log_debug("Set config %.*s = %s", (int)key.length, key.data, value ? "true" : "false");
    } catch (const std::exception& e) {
        fl_log_error("Failed to set config %.*s: %s", (int)key.length, key.data, e.what());
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set string configuration option (handles file paths for drives, ROMs, etc.)

static void vamiga_set_config_string(void* core_instance, FlString key, FlString value) {
    VAmigaCore* core = (VAmigaCore*)core_instance;
    if (!core || !core->vamiga) {
        return;
    }

    // Convert value to C string for vAmiga API
    char path_buffer[1024];
    if (value.length >= sizeof(path_buffer)) {
        fl_log_error("Path too long: %.*s", (int)value.length, value.data);
        return;
    }
    memcpy(path_buffer, value.data, value.length);
    path_buffer[value.length] = '\0';

    try {
        // Handle floppy drive paths
        if (string_equals(key, string_from_cstr("DF0"))) {
            core->vamiga->df0.insert(path_buffer, false);
            core->disk_paths[0] = string_copy(core->arena, value);
            fl_log_info("Inserted disk in DF0: %s", path_buffer);
        } else if (string_equals(key, string_from_cstr("DF1"))) {
            core->vamiga->df1.insert(path_buffer, false);
            core->disk_paths[1] = string_copy(core->arena, value);
            fl_log_info("Inserted disk in DF1: %s", path_buffer);
        } else if (string_equals(key, string_from_cstr("DF2"))) {
            core->vamiga->df2.insert(path_buffer, false);
            core->disk_paths[2] = string_copy(core->arena, value);
            fl_log_info("Inserted disk in DF2: %s", path_buffer);
        } else if (string_equals(key, string_from_cstr("DF3"))) {
            core->vamiga->df3.insert(path_buffer, false);
            core->disk_paths[3] = string_copy(core->arena, value);
            fl_log_info("Inserted disk in DF3: %s", path_buffer);
        } else {
            fl_log_warning("Unknown string config key for vAmiga: %.*s", (int)key.length, key.data);
        }
    } catch (const std::exception& e) {
        fl_log_error("Failed to set string config %.*s: %s", (int)key.length, key.data, e.what());
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get string configuration option

static FlString vamiga_get_config_string(void* core_instance, FlString key) {
    VAmigaCore* core = (VAmigaCore*)core_instance;
    if (!core) {
        return string_empty();
    }

    if (string_equals(key, string_from_cstr("DF0"))) {
        return core->disk_paths[0];
    } else if (string_equals(key, string_from_cstr("DF1"))) {
        return core->disk_paths[1];
    } else if (string_equals(key, string_from_cstr("DF2"))) {
        return core->disk_paths[2];
    } else if (string_equals(key, string_from_cstr("DF3"))) {
        return core->disk_paths[3];
    }

    return string_empty();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core API table

static RpEmuAPI s_vamiga_api = {
    .get_info = vamiga_get_info,
    .create = vamiga_create,
    .destroy = vamiga_destroy,
    .mount_media = vamiga_mount_media,
    .unmount_media = vamiga_unmount_media,
    .mount_media_list = vamiga_mount_media_list,
    .run_frame = vamiga_run_frame,
    .reset = vamiga_reset,
    .hard_reset = vamiga_hard_reset,
    .get_video_spec = vamiga_get_video_spec,
    .get_audio_spec = vamiga_get_audio_spec,
    .get_state = vamiga_get_state,
    .copy_audio = vamiga_copy_audio,
    .serialize = vamiga_serialize,
    .deserialize = vamiga_deserialize,
    .set_config_bitmask = nullptr, // Not used by vAmiga (FPGA cores only)
    .set_config_int = vamiga_set_config_int,
    .set_config_float = vamiga_set_config_float,
    .set_config_bool = vamiga_set_config_bool,
    .set_config_string = vamiga_set_config_string,
    .get_config_string = vamiga_get_config_string,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Plugin entry point

extern "C" RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void) {
    return &s_vamiga_api;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ABI version export. The host resolves this before it reads the vtable above and refuses the
// plugin when the answer is not the version it was built for.

RP_EMU_PLUGIN_ABI_VERSION_EXPORT()

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
