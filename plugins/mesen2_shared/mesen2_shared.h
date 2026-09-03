///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mesen2 Shared Components - Common code for all Mesen2-based core plugins
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <flowi/arena/arena.h>
#include "replay/emu_plugin.h"
#include <flowi/core/log_macros.h>
#include "replay/plugin_info.h"
#include <flowi/core/log_macros.h>
#include <flowi/string/string.h>
#include <flowi/core/types.h>

#include "Core/Shared/Audio/SoundMixer.h"
#include "Core/Shared/BaseControlDevice.h"
#include "Core/Shared/BaseControlManager.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/Interfaces/IInputProvider.h"
#include "Core/Shared/SettingTypes.h"
#include "Core/Shared/TimingInfo.h"
#include "Core/Shared/Video/VideoRenderer.h"
#include "Utilities/FolderUtilities.h"
#include "Utilities/SimpleLock.h"
#include "Utilities/VirtualFile.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FrameCaptureRenderer - Captures rendered frames from Mesen2 with R/B channel swap for SDL compatibility

class Mesen2FrameCaptureRenderer : public IRenderingDevice {
  public:
    Mesen2FrameCaptureRenderer(Emulator* emu, uint32_t default_width, uint32_t default_height)
        : _emu(emu), _writeWidth(default_width), _writeHeight(default_height), _readWidth(default_width),
          _readHeight(default_height) {
        _writeBuffer.resize(default_width * default_height);
        _readBuffer.resize(default_width * default_height);
        memset(_writeBuffer.data(), 0, _writeBuffer.size() * sizeof(uint32_t));
        memset(_readBuffer.data(), 0, _readBuffer.size() * sizeof(uint32_t));
        _emu->GetVideoRenderer()->RegisterRenderingDevice(this);
    }

    ~Mesen2FrameCaptureRenderer() override {}

    void UpdateFrame(RenderedFrame& frame) override {
        size_t frameSize = frame.Width * frame.Height;
        auto lock = _frameLock.AcquireSafe();
        if (_writeBuffer.size() < frameSize) {
            _writeBuffer.resize(frameSize);
        }
        // Swap R and B channels: Mesen2 outputs ARGB, SDL expects ABGR
        const uint32_t* src = static_cast<const uint32_t*>(frame.FrameBuffer);
        uint32_t* dst = _writeBuffer.data();
        for (size_t i = 0; i < frameSize; i++) {
            uint32_t pixel = src[i];
            uint32_t a = pixel & 0xFF000000;
            uint32_t r = (pixel >> 16) & 0xFF;
            uint32_t g = pixel & 0x0000FF00;
            uint32_t b = pixel & 0xFF;
            dst[i] = a | (b << 16) | g | r;
        }
        _writeWidth = frame.Width;
        _writeHeight = frame.Height;
        _newFrameReady = true;
    }

    void ClearFrame() override {
        auto lock = _frameLock.AcquireSafe();
        memset(_writeBuffer.data(), 0, _writeBuffer.size() * sizeof(uint32_t));
        _newFrameReady = true;
    }

    void Render(RenderSurfaceInfo& emuHud, RenderSurfaceInfo& scriptHud) override {
        (void)emuHud;
        (void)scriptHud;
    }

    void Reset() override {}

    void SetExclusiveFullscreenMode(bool fullscreen, void* windowHandle) override {
        (void)fullscreen;
        (void)windowHandle;
    }

    const uint32_t* SwapAndGetFrameBuffer() {
        auto lock = _frameLock.AcquireSafe();
        if (_newFrameReady) {
            std::swap(_readBuffer, _writeBuffer);
            _readWidth = _writeWidth;
            _readHeight = _writeHeight;
            _newFrameReady = false;
        }
        return _readBuffer.data();
    }

    uint32_t GetFrameWidth() const {
        return _readWidth;
    }
    uint32_t GetFrameHeight() const {
        return _readHeight;
    }

  private:
    Emulator* _emu;
    SimpleLock _frameLock;
    std::vector<uint32_t> _writeBuffer;
    std::vector<uint32_t> _readBuffer;
    uint32_t _writeWidth;
    uint32_t _writeHeight;
    uint32_t _readWidth;
    uint32_t _readHeight;
    bool _newFrameReady = false;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mesen2AudioCaptureDevice - Captures audio samples from Mesen2 for the frontend
//
// Audio flow:
// 1. Mesen2 calls PlayBuffer() with int16_t samples from its audio thread
// 2. We convert to float and store in the write buffer
// 3. On frame boundary (ProcessEndOfFrame), we swap buffers
// 4. Frontend reads from the read buffer via GetAudioBuffer()

class Mesen2AudioCaptureDevice : public IAudioDevice {
  public:
    static constexpr uint32_t MAX_SAMPLES_PER_FRAME = 2048; // ~1 frame at 48kHz/60fps = ~800 samples

    Mesen2AudioCaptureDevice() {
        _writeBuffer.reserve(MAX_SAMPLES_PER_FRAME * 2); // Stereo
        _readBuffer.reserve(MAX_SAMPLES_PER_FRAME * 2);
    }

    void PlayBuffer(int16_t* buffer, uint32_t sampleCount, uint32_t sampleRate, bool isStereo) override {
        (void)sampleRate; // We report 48kHz to frontend, Mesen2 typically outputs at this rate

        auto lock = _audioLock.AcquireSafe();

        // Calculate output size (always stereo)
        uint32_t outputSamples = sampleCount * 2;

        // Ensure we have space (grow if needed, but cap to avoid runaway)
        if (_writeBuffer.size() + outputSamples > MAX_SAMPLES_PER_FRAME * 4) {
            // Buffer overflow - drop oldest samples
            _writeBuffer.clear();
        }

        // Convert int16_t [-32768, 32767] to float [-1.0, 1.0]
        // Output is always interleaved stereo
        if (isStereo) {
            for (uint32_t i = 0; i < sampleCount * 2; i++) {
                _writeBuffer.push_back(static_cast<float>(buffer[i]) / 32768.0f);
            }
        } else {
            // Mono input - duplicate each sample for L/R channels
            for (uint32_t i = 0; i < sampleCount; i++) {
                float sample = static_cast<float>(buffer[i]) / 32768.0f;
                _writeBuffer.push_back(sample); // Left
                _writeBuffer.push_back(sample); // Right
            }
        }
    }

    void Stop() override {
        auto lock = _audioLock.AcquireSafe();
        _writeBuffer.clear();
        _readBuffer.clear();
    }

    void Pause() override {}

    void ProcessEndOfFrame() override {
        // NOTE: SoundMixer calls this after every PlayBuffer() call, which would
        // clear our buffer immediately. We make this a no-op and use SwapBuffers()
        // instead, which is called explicitly by our frontend at frame boundaries.
    }

    std::string GetAvailableDevices() override {
        return "";
    }

    void SetAudioDevice(std::string deviceName) override {
        (void)deviceName;
    }

    AudioStatistics GetStatistics() override {
        return {};
    }

    // Frontend access methods - call SwapBuffers() first, then read from the buffer

    void SwapBuffers() {
        // Swap buffers at frame boundary - called by frontend, not by SoundMixer
        auto lock = _audioLock.AcquireSafe();
        std::swap(_readBuffer, _writeBuffer);
        _writeBuffer.clear();
    }

    const float* GetAudioBuffer() const {
        return _readBuffer.empty() ? nullptr : _readBuffer.data();
    }

    uint32_t GetAudioFrameCount() const {
        // Frame count = total samples / channels (2 for stereo)
        return static_cast<uint32_t>(_readBuffer.size() / 2);
    }

  private:
    SimpleLock _audioLock;
    std::vector<float> _writeBuffer;
    std::vector<float> _readBuffer;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mesen2InputProvider - Maps RpEmuInputState to Mesen2 controller input
//
// Button mappings per console (ConsoleType enum values):
// Nes:      Up=0, Down=1, Left=2, Right=3, Start=4, Select=5, B=6, A=7
// Snes:     A=0, B=1, X=2, Y=3, L=4, R=5, Select=6, Start=7, Up=8, Down=9, Left=10, Right=11
// Gameboy:  Up=0, Down=1, Left=2, Right=3, Start=4, Select=5, B=6, A=7 (also handles GBC)
// Gba:      Up=0, Down=1, Left=2, Right=3, Start=4, Select=5, B=6, A=7, L=8, R=9
// Sms:      Up=0, Down=1, Left=2, Right=3, B=4, A=5, Pause=6 (also handles Game Gear)
// PcEngine: Up=0, Down=1, Left=2, Right=3, Select=4, Run=5, I=6, II=7 (also handles TurboGrafx-16)
// Ws:       Up=0, Down=1, Left=2, Right=3, Up2=4, Down2=5, Left2=6, Right2=7, Sound=8, Start=9, B=10, A=11 (also
// handles WSC)

class Mesen2InputProvider : public IInputProvider {
  public:
    Mesen2InputProvider(Emulator* emu, ConsoleType console_type)
        : _emu(emu), _consoleType(console_type), _input(nullptr) {
        _emu->RegisterInputProvider(this);
    }

    virtual ~Mesen2InputProvider() {
        _emu->UnregisterInputProvider(this);
    }

    void SetInputState(const RpEmuInputState* input) {
        _input = input;
    }

    bool SetInput(BaseControlDevice* device) override {
        if (!_input || !device) {
            return false;
        }

        // Only handle port 0 (player 1) for now
        if (device->GetPort() != 0) {
            return false;
        }

        device->ClearState();

        switch (_consoleType) {
            case ConsoleType::Nes:
                // NES: Up=0, Down=1, Left=2, Right=3, Start=4, Select=5, B=6, A=7
                device->SetBitValue(0, _input->buttons[RpEmuButton_Up]);
                device->SetBitValue(1, _input->buttons[RpEmuButton_Down]);
                device->SetBitValue(2, _input->buttons[RpEmuButton_Left]);
                device->SetBitValue(3, _input->buttons[RpEmuButton_Right]);
                device->SetBitValue(4, _input->buttons[RpEmuButton_Start]);
                device->SetBitValue(5, _input->buttons[RpEmuButton_Select]);
                device->SetBitValue(6, _input->buttons[RpEmuButton_B]);
                device->SetBitValue(7, _input->buttons[RpEmuButton_A]);
                break;

            case ConsoleType::Snes:
                // SNES: A=0, B=1, X=2, Y=3, L=4, R=5, Select=6, Start=7, Up=8, Down=9, Left=10, Right=11
                device->SetBitValue(0, _input->buttons[RpEmuButton_A]);
                device->SetBitValue(1, _input->buttons[RpEmuButton_B]);
                device->SetBitValue(2, _input->buttons[RpEmuButton_X]);
                device->SetBitValue(3, _input->buttons[RpEmuButton_Y]);
                device->SetBitValue(4, _input->buttons[RpEmuButton_L]);
                device->SetBitValue(5, _input->buttons[RpEmuButton_R]);
                device->SetBitValue(6, _input->buttons[RpEmuButton_Select]);
                device->SetBitValue(7, _input->buttons[RpEmuButton_Start]);
                device->SetBitValue(8, _input->buttons[RpEmuButton_Up]);
                device->SetBitValue(9, _input->buttons[RpEmuButton_Down]);
                device->SetBitValue(10, _input->buttons[RpEmuButton_Left]);
                device->SetBitValue(11, _input->buttons[RpEmuButton_Right]);
                break;

            case ConsoleType::Gameboy:
                // GB/GBC: Up=0, Down=1, Left=2, Right=3, Start=4, Select=5, B=6, A=7
                device->SetBitValue(0, _input->buttons[RpEmuButton_Up]);
                device->SetBitValue(1, _input->buttons[RpEmuButton_Down]);
                device->SetBitValue(2, _input->buttons[RpEmuButton_Left]);
                device->SetBitValue(3, _input->buttons[RpEmuButton_Right]);
                device->SetBitValue(4, _input->buttons[RpEmuButton_Start]);
                device->SetBitValue(5, _input->buttons[RpEmuButton_Select]);
                device->SetBitValue(6, _input->buttons[RpEmuButton_B]);
                device->SetBitValue(7, _input->buttons[RpEmuButton_A]);
                break;

            case ConsoleType::Gba:
                // GBA: Up=0, Down=1, Left=2, Right=3, Start=4, Select=5, B=6, A=7, L=8, R=9
                device->SetBitValue(0, _input->buttons[RpEmuButton_Up]);
                device->SetBitValue(1, _input->buttons[RpEmuButton_Down]);
                device->SetBitValue(2, _input->buttons[RpEmuButton_Left]);
                device->SetBitValue(3, _input->buttons[RpEmuButton_Right]);
                device->SetBitValue(4, _input->buttons[RpEmuButton_Start]);
                device->SetBitValue(5, _input->buttons[RpEmuButton_Select]);
                device->SetBitValue(6, _input->buttons[RpEmuButton_B]);
                device->SetBitValue(7, _input->buttons[RpEmuButton_A]);
                device->SetBitValue(8, _input->buttons[RpEmuButton_L]);
                device->SetBitValue(9, _input->buttons[RpEmuButton_R]);
                break;

            case ConsoleType::Sms:
                // SMS/Game Gear: Up=0, Down=1, Left=2, Right=3, B=4, A=5, Pause=6
                device->SetBitValue(0, _input->buttons[RpEmuButton_Up]);
                device->SetBitValue(1, _input->buttons[RpEmuButton_Down]);
                device->SetBitValue(2, _input->buttons[RpEmuButton_Left]);
                device->SetBitValue(3, _input->buttons[RpEmuButton_Right]);
                device->SetBitValue(4, _input->buttons[RpEmuButton_B]);
                device->SetBitValue(5, _input->buttons[RpEmuButton_A]);
                device->SetBitValue(6, _input->buttons[RpEmuButton_Start]); // Pause mapped to Start
                break;

            case ConsoleType::PcEngine:
                // PCE/TurboGrafx-16: Up=0, Down=1, Left=2, Right=3, Select=4, Run=5, I=6, II=7
                device->SetBitValue(0, _input->buttons[RpEmuButton_Up]);
                device->SetBitValue(1, _input->buttons[RpEmuButton_Down]);
                device->SetBitValue(2, _input->buttons[RpEmuButton_Left]);
                device->SetBitValue(3, _input->buttons[RpEmuButton_Right]);
                device->SetBitValue(4, _input->buttons[RpEmuButton_Select]);
                device->SetBitValue(5, _input->buttons[RpEmuButton_Start]); // Run mapped to Start
                device->SetBitValue(6, _input->buttons[RpEmuButton_A]);     // I mapped to A
                device->SetBitValue(7, _input->buttons[RpEmuButton_B]);     // II mapped to B
                break;

            case ConsoleType::Ws:
                // WS/WSC: Up=0, Down=1, Left=2, Right=3, Up2=4, Down2=5, Left2=6, Right2=7, Sound=8, Start=9, B=10,
                // A=11
                device->SetBitValue(0, _input->buttons[RpEmuButton_Up]);
                device->SetBitValue(1, _input->buttons[RpEmuButton_Down]);
                device->SetBitValue(2, _input->buttons[RpEmuButton_Left]);
                device->SetBitValue(3, _input->buttons[RpEmuButton_Right]);
                // Up2/Down2/Left2/Right2 (4-7) could be mapped to YXBA in rotated mode
                device->SetBitValue(4, _input->buttons[RpEmuButton_Y]);      // Up2
                device->SetBitValue(5, _input->buttons[RpEmuButton_X]);      // Down2
                device->SetBitValue(6, _input->buttons[RpEmuButton_L]);      // Left2
                device->SetBitValue(7, _input->buttons[RpEmuButton_R]);      // Right2
                device->SetBitValue(8, _input->buttons[RpEmuButton_Select]); // Sound
                device->SetBitValue(9, _input->buttons[RpEmuButton_Start]);
                device->SetBitValue(10, _input->buttons[RpEmuButton_B]);
                device->SetBitValue(11, _input->buttons[RpEmuButton_A]);
                break;

            default:
                return false;
        }

        return true;
    }

  private:
    Emulator* _emu;
    ConsoleType _consoleType;
    const RpEmuInputState* _input;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mesen2SystemInfo - System-specific configuration for each console type

struct Mesen2SystemInfo {
    const char* emu_name;
    const char* system_name;
    const char* extensions;
    bool requires_bios;
    uint32_t default_width;
    uint32_t default_height;
    float default_fps;
    CpuType cpu_type;
    // Visible area (for overscan handling)
    uint32_t visible_x;
    uint32_t visible_y;
    uint32_t visible_width;
    uint32_t visible_height;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mesen2CoreBase - Common state for all Mesen2-based cores

struct Mesen2CoreBase {
    FlArena* arena;
    std::unique_ptr<Emulator> emulator;
    std::unique_ptr<Mesen2FrameCaptureRenderer> renderer;
    std::unique_ptr<Mesen2AudioCaptureDevice> audioDevice;
    std::unique_ptr<Mesen2InputProvider> inputProvider;
    RpEmuLifecycleState state;
    RpVideoSpec video_spec;
    FlString rom_path;
    CpuType cpu_type; // Stored for timing info lookup
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Common helper functions for Mesen2 cores

inline Mesen2CoreBase* mesen2_create_core(FlArena* arena, const Mesen2SystemInfo& info, const char* log_name) {
    fl_log_info("Creating Mesen2 %s core", log_name);

    Mesen2CoreBase* core = (Mesen2CoreBase*)arena_alloc_raw_zero(arena, sizeof(Mesen2CoreBase), alignof(Mesen2CoreBase));
    core->arena = arena;
    core->state = RpEmuLifecycleState_Idle;
    core->cpu_type = info.cpu_type;

    try {
        // Set up home folder for Mesen2
        const char* home = getenv("HOME");
        if (home) {
            char mesen_home[512];
            snprintf(mesen_home, sizeof(mesen_home), "%s/.replay2/system/mesen2", home);
            FolderUtilities::SetHomeFolder(mesen_home);
        }

        // Create emulator instance
        core->emulator = std::make_unique<Emulator>();
        core->emulator->Initialize(false);

        // Create and register audio device
        core->audioDevice = std::make_unique<Mesen2AudioCaptureDevice>();
        core->emulator->GetSoundMixer()->RegisterAudioDevice(core->audioDevice.get());

        // Create and register video renderer
        core->renderer = std::make_unique<Mesen2FrameCaptureRenderer>(core->emulator.get(), info.default_width,
                                                                      info.default_height);

        // Initialize video spec
        core->video_spec.width = info.default_width;
        core->video_spec.height = info.default_height;
        core->video_spec.pixel_format = RpPixelFormat_Xrgb8888;
        core->video_spec.fps = info.default_fps;
        core->video_spec.visible_x = info.visible_x;
        core->video_spec.visible_y = info.visible_y;
        core->video_spec.visible_width = info.visible_width;
        core->video_spec.visible_height = info.visible_height;

        // Configure emulator settings
        EmuSettings* settings = core->emulator->GetSettings();
        settings->SetFlag(EmulationFlags::MaximumSpeed);

        // Configure audio - set all channel volumes to 100 (default is 0 for some systems!)
        // Note: Only NES, SMS, and CV have ChannelVolumes default to 0
        //       SNES already defaults to 100, other systems don't have this config

        // NES config - volumes default to 0
        NesConfig nesConfig = settings->GetNesConfig();
        for (int i = 0; i < 11; i++) {
            nesConfig.ChannelVolumes[i] = 100;
            nesConfig.ChannelPanning[i] = 0;
        }
        settings->SetNesConfig(nesConfig);

        // SMS config - volumes default to 0
        SmsConfig smsConfig = settings->GetSmsConfig();
        for (int i = 0; i < 4; i++) {
            smsConfig.ChannelVolumes[i] = 100;
        }
        settings->SetSmsConfig(smsConfig);

        // ColecoVision config - volumes default to 0
        CvConfig cvConfig = settings->GetCvConfig();
        for (int i = 0; i < 4; i++) {
            cvConfig.ChannelVolumes[i] = 100;
        }
        settings->SetCvConfig(cvConfig);

        // GBA: Skip boot screen when BIOS is not available.
        // Without a real BIOS, the CPU would execute zeroed memory at address 0
        // and never jump to the ROM entry point, resulting in a black screen.
        if (info.cpu_type == CpuType::Gba) {
            string firmwarePath = FolderUtilities::CombinePath(FolderUtilities::GetFirmwareFolder(), "gba_bios.bin");
            VirtualFile biosFile(firmwarePath);
            if (!biosFile.IsValid() || biosFile.GetSize() != 0x4000) {
                fl_log_info("GBA BIOS not found, enabling SkipBootScreen");
                GbaConfig gbaConfig = settings->GetGbaConfig();
                gbaConfig.SkipBootScreen = true;
                settings->SetGbaConfig(gbaConfig);
            }
        }

        fl_log_info("Mesen2 %s core created", log_name);
    } catch (const std::exception& e) {
        fl_log_error("Mesen2 %s core creation failed: %s", log_name, e.what());
        return nullptr;
    }

    return core;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline void mesen2_destroy_core(Mesen2CoreBase* core, const char* log_name) {
    if (!core) {
        return;
    }

    fl_log_info("Destroying Mesen2 %s core", log_name);

    if (core->emulator) {
        core->inputProvider.reset();
        core->emulator->Stop(false);
        core->emulator->Release();
        core->renderer.reset();
        core->audioDevice.reset();
        core->emulator.reset();
    }

    core->state = RpEmuLifecycleState_Idle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline bool mesen2_mount_media(Mesen2CoreBase* core, const char* path) {
    if (!core || !core->emulator) {
        fl_log_error("Invalid core instance");
        return false;
    }

    fl_log_info("Loading ROM: %s", path);

    try {
        VirtualFile romFile(path);
        VirtualFile patchFile;
        bool success = core->emulator->LoadRom(romFile, patchFile);

        if (success) {
            core->state = RpEmuLifecycleState_Running;
            core->rom_path = string_copy(core->arena, string_from_cstr(path));

            // Create input provider for this console type
            ConsoleType consoleType = core->emulator->GetConsoleType();
            core->inputProvider = std::make_unique<Mesen2InputProvider>(core->emulator.get(), consoleType);

            // Update FPS based on loaded ROM's region
            TimingInfo timing = core->emulator->GetTimingInfo(core->cpu_type);
            core->video_spec.fps = (float)timing.Fps;

            fl_log_info("ROM loaded: FPS=%.2f, type=%d", timing.Fps, (int)consoleType);
            return true;
        }

        fl_log_error("Failed to load ROM");
        return false;
    } catch (const std::exception& e) {
        fl_log_error("Failed to load ROM: %s", e.what());
        return false;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline void mesen2_unmount_media(Mesen2CoreBase* core) {
    if (!core || !core->emulator) {
        return;
    }

    core->inputProvider.reset();
    core->emulator->Stop(false);
    core->state = RpEmuLifecycleState_Idle;
    core->rom_path = {};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline void mesen2_run_frame(Mesen2CoreBase* core, RpEmuFrameContext* ctx) {
    if (!core || !core->emulator || core->state != RpEmuLifecycleState_Running) {
        return;
    }

    // Update input state for this frame
    if (core->inputProvider && ctx->input) {
        core->inputProvider->SetInputState(ctx->input);
    }

    // Capture the latest frame from Mesen2's rendering thread
    if (core->renderer) {
        ctx->video_buffer = core->renderer->SwapAndGetFrameBuffer();
        ctx->video_pitch = core->renderer->GetFrameWidth() * sizeof(uint32_t);

        // Update video spec if resolution changed (hi-res modes, etc.)
        if (core->renderer->GetFrameWidth() != core->video_spec.width
            || core->renderer->GetFrameHeight() != core->video_spec.height) {
            core->video_spec.width = core->renderer->GetFrameWidth();
            core->video_spec.height = core->renderer->GetFrameHeight();
            core->video_spec.visible_width = core->video_spec.width;
            core->video_spec.visible_height = core->video_spec.height;
        }
    }

    // Capture audio samples from this frame
    if (core->audioDevice) {
        // Swap audio buffers to get the samples accumulated during this frame
        // Note: We call SwapBuffers() explicitly here, NOT ProcessEndOfFrame() which is
        // called by SoundMixer after every PlayBuffer() call (and would clear our data)
        core->audioDevice->SwapBuffers();
        ctx->audio_buffer = core->audioDevice->GetAudioBuffer();
        ctx->audio_frames = core->audioDevice->GetAudioFrameCount();
    } else {
        ctx->audio_buffer = nullptr;
        ctx->audio_frames = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline void mesen2_reset(Mesen2CoreBase* core) {
    if (core && core->emulator) {
        fl_log_info("Soft reset");
        core->emulator->Reset();
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline void mesen2_hard_reset(Mesen2CoreBase* core) {
    if (core && core->emulator) {
        fl_log_info("Hard reset (power cycle)");
        core->emulator->PowerCycle();
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline void mesen2_get_video_spec(Mesen2CoreBase* core, RpVideoSpec* spec) {
    if (core) {
        *spec = core->video_spec;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline void mesen2_get_audio_spec(RpAudioSpec* spec) {
    if (spec) {
        spec->sample_rate = 48000;
        spec->channels = 2;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline RpEmuLifecycleState mesen2_get_state(Mesen2CoreBase* core) {
    return core ? core->state : RpEmuLifecycleState_Idle;
}
