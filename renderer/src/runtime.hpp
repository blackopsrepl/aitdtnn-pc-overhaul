#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <string>

namespace aitd4 {

struct OverhaulConfig {
    int logical_width{0};
    int logical_height{0};
    int msaa{4};
    int anisotropy{16};
    bool vsync{true};
    bool deband{true};
    bool dither{true};
    bool fix_color_depth{true};
    bool fix_mask_seams{true};
    bool development_hot_reload{false};
    bool development_capture{false};
    bool crt_enabled{true};
    int crt_signal_width{640};
    int crt_signal_height{480};
    float crt_mask_strength{0.20f};
    float crt_scanline_strength{0.25f};
    float crt_bloom_strength{0.08f};
    float crt_halation_strength{0.04f};
};

extern OverhaulConfig g_config;
extern FILE* g_log;
extern char g_game_directory[MAX_PATH];
extern char g_module_directory[MAX_PATH];
extern char g_ini_path[MAX_PATH];

enum class ExecutableProfile {
    unknown,
    english_15_slot_no_cd,
    english_retail_cd,
};

extern ExecutableProfile g_executable_profile;

void log_line(const char* format, ...);
bool initialize_runtime(HMODULE self);
bool reload_runtime_config();
bool validate_supported_executable();
bool is_retail_executable();

}  // namespace aitd4
