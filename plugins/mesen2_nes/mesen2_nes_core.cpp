///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mesen2 NES Core Plugin - Wrapper around Mesen2 NES emulator
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Core/NES/NesConsole.h"
#include "mesen2_shared.h"

// NES 2C02 PPU palette - "Composite Direct" by FirebrandX
// A popular palette that provides a more natural look matching real hardware
// This needs to be copied to NesConfig.UserPalette before creating the video filter
static const uint32_t s_default_nes_palette[64] = {
    0xFF656565, 0xFF00127D, 0xFF18008E, 0xFF360082, 0xFF56005D, 0xFF5A0018, 0xFF4F0500, 0xFF381900,
    0xFF1D3100, 0xFF003D00, 0xFF004100, 0xFF003B17, 0xFF002E55, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFAFAFAF, 0xFF194EC8, 0xFF472FE3, 0xFF6B1FD7, 0xFF931BAE, 0xFF9E1A5E, 0xFF993200, 0xFF7B4B00,
    0xFF5B6700, 0xFF267A00, 0xFF008200, 0xFF007A3E, 0xFF006E8A, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFFFFFFF, 0xFF64A9FF, 0xFF8E89FF, 0xFFB676FF, 0xFFE06FFF, 0xFFEF6CC4, 0xFFF0806A, 0xFFD8982C,
    0xFFB9B40A, 0xFF83CB0C, 0xFF5BD63F, 0xFF4AD17E, 0xFF4DC7CB, 0xFF4C4C4C, 0xFF000000, 0xFF000000,
    0xFFFFFFFF, 0xFFC7E5FF, 0xFFD9D9FF, 0xFFE9D1FF, 0xFFF9CEFF, 0xFFFFCCF1, 0xFFFFD4CB, 0xFFF8DFB1,
    0xFFEDEAA4, 0xFFD6F4A4, 0xFFC5F8B8, 0xFFBEF6D3, 0xFFBFF1F1, 0xFFB9B9B9, 0xFF000000, 0xFF000000,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NES system configuration

static const Mesen2SystemInfo s_nes_info = {
    .emu_name = "Mesen2",
    .system_name = "Nintendo Entertainment System",
    .extensions = "nes|fds|unf|unif|nsf|nsfe",
    .requires_bios = false,
    .default_width = 256,
    .default_height = 240,
    .default_fps = 60.0988f,
    .cpu_type = CpuType::Nes,
    .visible_x = 0,
    .visible_y = 8,
    .visible_width = 256,
    .visible_height = 224,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void mesen2_nes_get_info(RpEmuInfo* info) {
    info->emu_name = s_nes_info.emu_name;
    info->emu_version = "2.0";
    info->system_name = s_nes_info.system_name;
    info->supported_extensions = s_nes_info.extensions;
    info->requires_bios = s_nes_info.requires_bios;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* mesen2_nes_create(FlArena* arena) {

    Mesen2CoreBase* core = mesen2_create_core(arena, s_nes_info, "NES");
    if (!core) {
        return nullptr;
    }

    // Initialize the default NES palette
    // NesConfig.UserPalette is zero-initialized by default, which causes black screen
    EmuSettings* settings = core->emulator->GetSettings();
    NesConfig& nesConfig = settings->GetNesConfig();
    memcpy(nesConfig.UserPalette, s_default_nes_palette, sizeof(s_default_nes_palette));

    return core;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void mesen2_nes_destroy(void* core_instance) {
    mesen2_destroy_core((Mesen2CoreBase*)core_instance, "NES");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool mesen2_nes_mount_media(void* core_instance, const char* path) {
    return mesen2_mount_media((Mesen2CoreBase*)core_instance, path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void mesen2_nes_unmount_media(void* core_instance) {
    mesen2_unmount_media((Mesen2CoreBase*)core_instance);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void mesen2_nes_run_frame(void* core_instance, RpEmuFrameContext* ctx) {
    mesen2_run_frame((Mesen2CoreBase*)core_instance, ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void mesen2_nes_reset(void* core_instance) {
    mesen2_reset((Mesen2CoreBase*)core_instance);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void mesen2_nes_hard_reset(void* core_instance) {
    mesen2_hard_reset((Mesen2CoreBase*)core_instance);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void mesen2_nes_get_video_spec(void* core_instance, RpVideoSpec* spec) {
    mesen2_get_video_spec((Mesen2CoreBase*)core_instance, spec);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void mesen2_nes_get_audio_spec(void* core_instance, RpAudioSpec* spec) {
    (void)core_instance;
    mesen2_get_audio_spec(spec);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RpEmuLifecycleState mesen2_nes_get_state(void* core_instance) {
    return mesen2_get_state((Mesen2CoreBase*)core_instance);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static size_t mesen2_nes_serialize(void* ci, void* b, size_t bs) {
    (void)ci;
    (void)b;
    (void)bs;
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool mesen2_nes_deserialize(void* ci, const void* b, size_t bs) {
    (void)ci;
    (void)b;
    (void)bs;
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void mesen2_nes_set_config_int(void* ci, FlString k, int32_t v) {
    (void)ci;
    (void)k;
    (void)v;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void mesen2_nes_set_config_float(void* ci, FlString k, float v) {
    (void)ci;
    (void)k;
    (void)v;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void mesen2_nes_set_config_bool(void* ci, FlString k, bool v) {
    (void)ci;
    (void)k;
    (void)v;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void mesen2_nes_set_config_string(void* ci, FlString k, FlString v) {
    (void)ci;
    (void)k;
    (void)v;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString mesen2_nes_get_config_string(void* ci, FlString k) {
    (void)ci;
    (void)k;
    return string_empty();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RpEmuAPI s_mesen2_nes_api = {
    .get_info = mesen2_nes_get_info,
    .create = mesen2_nes_create,
    .destroy = mesen2_nes_destroy,
    .mount_media = mesen2_nes_mount_media,
    .unmount_media = mesen2_nes_unmount_media,
    .run_frame = mesen2_nes_run_frame,
    .reset = mesen2_nes_reset,
    .hard_reset = mesen2_nes_hard_reset,
    .get_video_spec = mesen2_nes_get_video_spec,
    .get_audio_spec = mesen2_nes_get_audio_spec,
    .get_state = mesen2_nes_get_state,
    .copy_audio = nullptr,
    .serialize = mesen2_nes_serialize,
    .deserialize = mesen2_nes_deserialize,
    .set_config_bitmask = nullptr,
    .set_config_int = mesen2_nes_set_config_int,
    .set_config_float = mesen2_nes_set_config_float,
    .set_config_bool = mesen2_nes_set_config_bool,
    .set_config_string = mesen2_nes_set_config_string,
    .get_config_string = mesen2_nes_get_config_string,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void) {
    return &s_mesen2_nes_api;
}
