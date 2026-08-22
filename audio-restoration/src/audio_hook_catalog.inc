// Runtime asset records, logging and catalog loading.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <share.h>
#include <string>
#include <vector>

#include "audio_hook_api.hpp"
#include "audio_renderer.hpp"
#include "explicit_initializer.hpp"
#include "music_identity.hpp"
#include "miles_stream.hpp"

namespace {

namespace music_identity = aitd4::music_identity;

struct SequenceAsset {
    std::string container;
    std::string name;
    std::vector<std::uint8_t> bytes;
    int bank_id{-1};
};

struct ContainerIdentity {
    std::string container;
    int bank_id{-1};
    std::vector<std::array<std::uint8_t, 128>> maps;
};

struct ExecutableProfile {
    const char* name;
    std::uintptr_t dispatch_rva;
    std::uintptr_t handles_rva;
    std::uintptr_t sequence_state_rva;
    std::uintptr_t inverse_maps_rva;
};

struct ContainerResolution {
    const ContainerIdentity* sequence_identity{};
    const ContainerIdentity* bank_identity{};
    const SequenceAsset* sequence{};
    music_identity::Evidence evidence{music_identity::Evidence::none};
    int candidate_count{};
};

// The original CD executable and the later 15-slot-compatible executable use
// the same structures and dispatcher, shifted by fixed RVAs.
constexpr ExecutableProfile executable_profiles[] = {
    {"retail-cd", 0x97AAD, 0x1225EC, 0x121EB8, 0x121E84},
    {"15-slot",   0x97C0D, 0x1226EC, 0x121FB8, 0x121F84},
};

void* g_dispatch_trampoline = nullptr;
const ExecutableProfile* g_profile = nullptr;
HMODULE g_self = nullptr;
aitd4::ExplicitInitializer g_initializer;
char g_log_path[MAX_PATH]{};
char g_asset_root[MAX_PATH]{};
FILE* g_log = nullptr;
CRITICAL_SECTION g_log_lock{};
std::vector<SequenceAsset> g_sequences;
std::vector<ContainerIdentity> g_containers;
constexpr int sequence_slots = 5;
std::array<void*, sequence_slots> g_last_sequence_table{};
std::array<std::uint8_t*, sequence_slots> g_last_inverse_map{};
std::array<int, sequence_slots> g_last_bank_id{-1, -1, -1, -1, -1};
std::array<int, sequence_slots> g_last_map_selector{-1, -1, -1, -1, -1};
std::array<std::uint64_t, sequence_slots> g_last_bank_serial{};
std::array<std::string, sequence_slots> g_last_container{};
std::array<std::array<std::uint8_t, 16>, sequence_slots> g_channel_program{};
std::array<bool, sequence_slots> g_renderer_bound{};
CRITICAL_SECTION g_identity_lock{};
music_identity::LoadedIdentityRegistry g_identity_registry;
music_identity::LoadedBankRegistry g_bank_registry;
void* g_container_loader_trampoline{};

using ContainerLoaderFn = void* (__cdecl*)(const char*);
using MilesWaveOutOpenFn = std::int32_t (WINAPI*)(aitd4::MilesDriver*, LPHWAVEOUT*,
                                                  std::int32_t, LPWAVEFORMAT);
MilesWaveOutOpenFn g_miles_wave_out_open{};
using MilesAllocateSampleFn = aitd4::MilesSample (WINAPI*)(aitd4::MilesDriver);
MilesAllocateSampleFn g_miles_allocate_sample{};
std::unique_ptr<aitd4::MilesStream> g_miles_stream;

void log_line(const char* format, ...) {
    EnterCriticalSection(&g_log_lock);
    if (g_log) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        std::fprintf(g_log, "%02u:%02u:%02u.%03u ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        va_list args;
        va_start(args, format);
        std::vfprintf(g_log, format, args);
        va_end(args);
        std::fputc('\n', g_log);
    }
    LeaveCriticalSection(&g_log_lock);
}

void stream_log(const char* message) {
    log_line("%s", message ? message : "Miles AICA stream message unavailable");
}

void attach_miles_stream(aitd4::MilesDriver driver, const char* source) {
    if (!driver || !g_miles_stream || g_miles_stream->running()) return;
    log_line("Miles driver captured source=%s driver=%p", source, driver);
    if (!g_miles_stream->start(driver))
        log_line("Miles AICA stream creation failed; persistent PC music remains suppressed");
}

std::vector<std::string> split_tabs(const char* line) {
    std::vector<std::string> fields;
    const char* start = line;
    for (const char* p = line;; ++p) {
        if (*p == '\t' || *p == '\r' || *p == '\n' || *p == '\0') {
            fields.emplace_back(start, p);
            if (*p != '\t') break;
            start = p + 1;
        }
    }
    return fields;
}

bool read_file(const std::string& path, std::vector<std::uint8_t>& bytes) {
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") != 0 || !file) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::rewind(file);
    if (size <= 0) { std::fclose(file); return false; }
    bytes.resize(static_cast<std::size_t>(size));
    const bool ok = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    return ok;
}

bool locate_assets(HMODULE self) {
    char exe_path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, exe_path, MAX_PATH)) return false;
    char* slash = std::strrchr(exe_path, '\\');
    if (!slash) return false;
    *slash = '\0';
    std::snprintf(g_asset_root, MAX_PATH, "%s\\audio-restoration\\runtime-assets", exe_path);
    char manifest[MAX_PATH]{};
    std::snprintf(manifest, MAX_PATH, "%s\\sequences.tsv", g_asset_root);
    if (GetFileAttributesA(manifest) != INVALID_FILE_ATTRIBUTES) return true;

    char dll_path[MAX_PATH]{};
    if (!GetModuleFileNameA(self, dll_path, MAX_PATH)) return false;
    slash = std::strrchr(dll_path, '\\');
    if (!slash) return false;
    *slash = '\0';
    std::snprintf(g_asset_root, MAX_PATH, "%s\\runtime-assets", dll_path);
    std::snprintf(manifest, MAX_PATH, "%s\\sequences.tsv", g_asset_root);
    return GetFileAttributesA(manifest) != INVALID_FILE_ATTRIBUTES;
}

bool load_sequence_catalog() {
    char bank_path[MAX_PATH]{};
    std::snprintf(bank_path, MAX_PATH, "%s\\banks.tsv", g_asset_root);
    if (FILE* banks = nullptr; fopen_s(&banks, bank_path, "r") == 0 && banks) {
        char bank_line[512]{};
        std::fgets(bank_line, sizeof(bank_line), banks);
        while (std::fgets(bank_line, sizeof(bank_line), banks)) {
            auto fields = split_tabs(bank_line);
            if (fields.size() >= 3) {
                ContainerIdentity identity;
                identity.container = fields[0];
                identity.bank_id = std::atoi(fields[1].c_str());
                identity.maps.resize(std::atoi(fields[2].c_str()));
                for (auto& map : identity.maps) map.fill(0xFF);
                g_containers.emplace_back(std::move(identity));
            }
        }
        std::fclose(banks);
    }
    char maps_path[MAX_PATH]{};
    std::snprintf(maps_path, MAX_PATH, "%s\\maps.tsv", g_asset_root);
    if (FILE* maps = nullptr; fopen_s(&maps, maps_path, "r") == 0 && maps) {
        char map_line[512]{};
        std::fgets(map_line, sizeof(map_line), maps);
        while (std::fgets(map_line, sizeof(map_line), maps)) {
            auto fields = split_tabs(map_line);
            if (fields.size() < 4) continue;
            for (auto& identity : g_containers) {
                if (identity.container != fields[0]) continue;
                const auto map_index = std::atoi(fields[1].c_str());
                const auto global_voice = std::atoi(fields[2].c_str());
                const auto local_program = std::atoi(fields[3].c_str());
                if (map_index >= 0 && map_index < static_cast<int>(identity.maps.size()) &&
                    global_voice >= 0 && global_voice < 128)
                    identity.maps[map_index][global_voice] = static_cast<std::uint8_t>(local_program);
                break;
            }
        }
        std::fclose(maps);
    }
    char path[MAX_PATH]{};
    std::snprintf(path, MAX_PATH, "%s\\sequences.tsv", g_asset_root);
    FILE* file = nullptr;
    if (fopen_s(&file, path, "r") != 0 || !file) return false;
    char line[2048]{};
    if (!std::fgets(line, sizeof(line), file)) { std::fclose(file); return false; }
    while (std::fgets(line, sizeof(line), file)) {
        auto fields = split_tabs(line);
        if (fields.size() < 6) continue;
        SequenceAsset asset;
        asset.container = fields[0];
        asset.name = fields[2];
        for (const auto& identity : g_containers)
            if (identity.container == asset.container) { asset.bank_id = identity.bank_id; break; }
        std::string relative = fields[5];
        for (char& c : relative) if (c == '/') c = '\\';
        if (read_file(std::string(g_asset_root) + "\\" + relative, asset.bytes))
            g_sequences.emplace_back(std::move(asset));
    }
    std::fclose(file);
    return !g_sequences.empty();
}
