#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include "runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace aitd4 {

OverhaulConfig g_config;
FILE* g_log = nullptr;
char g_game_directory[MAX_PATH]{};
char g_module_directory[MAX_PATH]{};
char g_ini_path[MAX_PATH]{};
ExecutableProfile g_executable_profile{ExecutableProfile::unknown};
CRITICAL_SECTION g_log_lock{};

namespace {

constexpr char supported_15_slot_sha256[] =
    "5668118E0E19D569986500A1C805A85397C8681E7B672B49A68645462ECCC672";
constexpr char supported_retail_sha256[] =
    "320908AF4CE5C724B60A7EEA6A5AADE737D51D65AEE8506744FCE6E6DD0143E0";

bool ini_bool(const char* path, const char* section, const char* key, bool fallback) {
    return GetPrivateProfileIntA(section, key, fallback ? 1 : 0, path) != 0;
}

float ini_float(const char* path, const char* section, const char* key, float fallback,
                float minimum, float maximum) {
    char value[64]{};
    char fallback_text[32]{};
    std::snprintf(fallback_text, sizeof(fallback_text), "%.6g", fallback);
    GetPrivateProfileStringA(section, key, fallback_text, value, sizeof(value), path);
    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    if (end == value || !std::isfinite(parsed)) return fallback;
    return std::clamp(parsed, minimum, maximum);
}

void load_config_values() {
    g_config.logical_width = std::clamp(
        static_cast<int>(GetPrivateProfileIntA("Display", "LogicalWidth", 0, g_ini_path)),
        0, 16384);
    g_config.logical_height = std::clamp(
        static_cast<int>(GetPrivateProfileIntA("Display", "LogicalHeight", 0, g_ini_path)),
        0, 16384);
    g_config.msaa = std::clamp(
        static_cast<int>(GetPrivateProfileIntA("Graphics", "MSAA", 4, g_ini_path)), 0, 16);
    g_config.anisotropy = std::clamp(
        static_cast<int>(GetPrivateProfileIntA("Graphics", "Anisotropy", 16, g_ini_path)), 1, 16);
    g_config.vsync = ini_bool(g_ini_path, "Display", "VSync", true);
    g_config.deband = ini_bool(g_ini_path, "Graphics", "Deband", true);
    g_config.dither = ini_bool(g_ini_path, "Graphics", "Dither", true);
    g_config.fix_color_depth = ini_bool(g_ini_path, "Graphics", "FixColorDepth", true);
    g_config.fix_mask_seams = ini_bool(g_ini_path, "Graphics", "FixMaskSeams", true);
    g_config.development_hot_reload =
        ini_bool(g_ini_path, "Development", "HotReload", false);
    g_config.development_capture =
        ini_bool(g_ini_path, "Development", "Capture", false);
    g_config.crt_enabled = ini_bool(g_ini_path, "CRT", "Enabled", true);
    g_config.crt_signal_width = std::clamp(
        static_cast<int>(GetPrivateProfileIntA("CRT", "SignalWidth", 640, g_ini_path)),
        320, 1920);
    g_config.crt_signal_height = std::clamp(
        static_cast<int>(GetPrivateProfileIntA("CRT", "SignalHeight", 480, g_ini_path)),
        240, 1440);
    g_config.crt_mask_strength = ini_float(
        g_ini_path, "CRT", "MaskStrength", 0.20f, 0.0f, 1.0f);
    g_config.crt_scanline_strength = ini_float(
        g_ini_path, "CRT", "ScanlineStrength", 0.25f, 0.0f, 1.0f);
    g_config.crt_bloom_strength = ini_float(
        g_ini_path, "CRT", "BloomStrength", 0.08f, 0.0f, 0.5f);
    g_config.crt_halation_strength = ini_float(
        g_ini_path, "CRT", "HalationStrength", 0.04f, 0.0f, 0.25f);
#ifdef AITD4_TEST_HARNESS
    char test_value[32]{};
    if (GetEnvironmentVariableA("AITD4_TEST_LOGICAL_WIDTH", test_value, sizeof(test_value)))
        g_config.logical_width = std::clamp(std::atoi(test_value), 0, 16384);
    if (GetEnvironmentVariableA("AITD4_TEST_LOGICAL_HEIGHT", test_value, sizeof(test_value)))
        g_config.logical_height = std::clamp(std::atoi(test_value), 0, 16384);
    if (GetEnvironmentVariableA("AITD4_TEST_CRT_ENABLED", test_value, sizeof(test_value)))
        g_config.crt_enabled = std::atoi(test_value) != 0;
#endif
}

bool validate_crt_config() {
    if (!g_config.crt_enabled) return true;
    if (static_cast<long long>(g_config.crt_signal_width) * 3 !=
        static_cast<long long>(g_config.crt_signal_height) * 4) {
        log_line("CRT signal geometry must be exact 4:3; requested=%dx%d",
                 g_config.crt_signal_width, g_config.crt_signal_height);
        return false;
    }
    char preset[32]{};
    char mask_type[32]{};
    GetPrivateProfileStringA("CRT", "Preset", "Faithful", preset, sizeof(preset), g_ini_path);
    GetPrivateProfileStringA("CRT", "MaskType", "ApertureGrille", mask_type,
                             sizeof(mask_type), g_ini_path);
    if (_stricmp(preset, "Faithful") != 0 || _stricmp(mask_type, "ApertureGrille") != 0) {
        log_line("unsupported CRT preset/mask preset=%s mask=%s", preset, mask_type);
        return false;
    }
    const float curvature = ini_float(g_ini_path, "CRT", "Curvature", 0.0f, 0.0f, 1.0f);
    const float overscan = ini_float(g_ini_path, "CRT", "Overscan", 0.0f, 0.0f, 1.0f);
    const float aberration = ini_float(
        g_ini_path, "CRT", "ChromaticAberration", 0.0f, 0.0f, 1.0f);
    const float vignette = ini_float(g_ini_path, "CRT", "Vignette", 0.0f, 0.0f, 1.0f);
    if (curvature != 0.0f || overscan != 0.0f || aberration != 0.0f || vignette != 0.0f) {
        log_line("unsupported CRT distortion requested curvature=%.3f overscan=%.3f aberration=%.3f vignette=%.3f",
                 curvature, overscan, aberration, vignette);
        return false;
    }
    return true;
}

std::string sha256_file(const char* path) {
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD hash_size = 0;
    DWORD returned = 0;
    std::vector<unsigned char> object;
    std::vector<unsigned char> digest;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
              BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                                &returned, 0) >= 0 &&
              BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                                reinterpret_cast<PUCHAR>(&hash_size), sizeof(hash_size),
                                &returned, 0) >= 0;
    if (ok) {
        object.resize(object_size);
        digest.resize(hash_size);
        ok = BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) >= 0;
    }
    std::array<unsigned char, 64 * 1024> buffer{};
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            ok = false;
            break;
        }
        if (read == 0) break;
        ok = BCryptHashData(hash, buffer.data(), read, 0) >= 0;
    }
    if (ok) ok = BCryptFinishHash(hash, digest.data(), hash_size, 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (!ok) return {};
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.resize(digest.size() * 2);
    for (std::size_t i = 0; i < digest.size(); ++i) {
        result[i * 2] = hex[digest[i] >> 4];
        result[i * 2 + 1] = hex[digest[i] & 15];
    }
    return result;
}

}  // namespace

void log_line(const char* format, ...) {
    EnterCriticalSection(&g_log_lock);
    if (g_log) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        std::fprintf(g_log, "%02u:%02u:%02u.%03u ", now.wHour, now.wMinute, now.wSecond,
                     now.wMilliseconds);
        va_list args;
        va_start(args, format);
        std::vfprintf(g_log, format, args);
        va_end(args);
        std::fputc('\n', g_log);
        std::fflush(g_log);
    }
    LeaveCriticalSection(&g_log_lock);
}

bool initialize_runtime(HMODULE self) {
    InitializeCriticalSection(&g_log_lock);
    char exe_path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, exe_path, MAX_PATH)) return false;
    strcpy_s(g_game_directory, exe_path);
    char* slash = std::strrchr(g_game_directory, '\\');
    if (!slash) return false;
    *slash = '\0';
    if (!GetModuleFileNameA(self, g_module_directory, MAX_PATH)) return false;
    slash = std::strrchr(g_module_directory, '\\');
    if (!slash) return false;
    *slash = '\0';
    char log_path[MAX_PATH]{};
    std::snprintf(log_path, MAX_PATH, "%s\\aitd4-renderer.log", g_module_directory);
    fopen_s(&g_log, log_path, "w");
    if (g_log) setvbuf(g_log, nullptr, _IOFBF, 64 * 1024);
    std::snprintf(g_ini_path, MAX_PATH, "%s\\aitd4-overhaul.ini", g_module_directory);
    if (GetFileAttributesA(g_ini_path) == INVALID_FILE_ATTRIBUTES)
        std::snprintf(g_ini_path, MAX_PATH, "%s\\aitd4-overhaul.ini", g_game_directory);
    load_config_values();
    log_line("renderer hook initializing module=%p ini=%s", self, g_ini_path);
    return validate_crt_config();
}

bool reload_runtime_config() {
    const int active_logical_width = g_config.logical_width;
    const int active_logical_height = g_config.logical_height;
    const int active_msaa = g_config.msaa;
    const bool active_crt_enabled = g_config.crt_enabled;
    const int active_crt_signal_width = g_config.crt_signal_width;
    const int active_crt_signal_height = g_config.crt_signal_height;
    load_config_values();
    if (!validate_crt_config()) return false;
    if (g_config.logical_width != active_logical_width ||
        g_config.logical_height != active_logical_height || g_config.msaa != active_msaa) {
        log_line("logical resolution/MSAA changes require restart; active=%dx%d/%d requested=%dx%d/%d",
                 active_logical_width, active_logical_height, active_msaa,
                 g_config.logical_width, g_config.logical_height, g_config.msaa);
        g_config.logical_width = active_logical_width;
        g_config.logical_height = active_logical_height;
        g_config.msaa = active_msaa;
    }
    if (g_config.crt_enabled != active_crt_enabled ||
        g_config.crt_signal_width != active_crt_signal_width ||
        g_config.crt_signal_height != active_crt_signal_height) {
        log_line("CRT enable/signal geometry changes require restart; active=%d/%dx%d requested=%d/%dx%d",
                 active_crt_enabled ? 1 : 0, active_crt_signal_width, active_crt_signal_height,
                 g_config.crt_enabled ? 1 : 0, g_config.crt_signal_width,
                 g_config.crt_signal_height);
        g_config.crt_enabled = active_crt_enabled;
        g_config.crt_signal_width = active_crt_signal_width;
        g_config.crt_signal_height = active_crt_signal_height;
    }
    log_line("renderer configuration reloaded msaa=%d anisotropy=%d vsync=%d deband=%d dither=%d crt=%d mask=%.3f scanline=%.3f bloom=%.3f halation=%.3f",
             g_config.msaa, g_config.anisotropy, g_config.vsync ? 1 : 0,
             g_config.deband ? 1 : 0, g_config.dither ? 1 : 0,
             g_config.crt_enabled ? 1 : 0, g_config.crt_mask_strength,
             g_config.crt_scanline_strength, g_config.crt_bloom_strength,
             g_config.crt_halation_strength);
    return true;
}

bool validate_supported_executable() {
    char exe_path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, exe_path, MAX_PATH)) return false;
#ifdef AITD4_TEST_HARNESS
    if (std::strstr(exe_path, "aitd4-gl-harness")) {
        log_line("test harness executable accepted path=%s", exe_path);
        return true;
    }
#endif
    const auto actual = sha256_file(exe_path);
    if (actual == supported_15_slot_sha256) {
        g_executable_profile = ExecutableProfile::english_15_slot_no_cd;
    } else if (actual == supported_retail_sha256) {
        g_executable_profile = ExecutableProfile::english_retail_cd;
    } else {
        log_line("unsupported executable sha256=%s", actual.c_str());
        MessageBoxA(nullptr,
                    "AITD4 renderer refused to patch an unsupported alone4.exe.\n"
                    "The game was not started and no fallback was used.",
                    "AITD4 renderer", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return false;
    }
    log_line("executable verified profile=%s sha256=%s",
             is_retail_executable() ? "english-retail-cd" : "english-15-slot-no-cd",
             actual.c_str());
    return true;
}

bool is_retail_executable() {
    return g_executable_profile == ExecutableProfile::english_retail_cd;
}

}  // namespace aitd4
