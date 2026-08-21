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

struct LoadProvenance {
    std::string container;
    std::string path;
    std::uint64_t serial{};
};

struct ContainerResolution {
    const ContainerIdentity* identity{};
    const SequenceAsset* sequence{};
    music_identity::Evidence evidence{music_identity::Evidence::none};
    std::uint64_t load_serial{};
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
std::array<std::string, sequence_slots> g_last_container{};
std::array<std::array<std::uint8_t, 16>, sequence_slots> g_channel_program{};
std::array<bool, sequence_slots> g_renderer_bound{};
CRITICAL_SECTION g_provenance_lock{};
std::vector<LoadProvenance> g_load_provenance;
std::uint64_t g_load_serial{};

using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                     DWORD, DWORD, HANDLE);
CreateFileAFn g_create_file_a = nullptr;
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

bool readable(const void* pointer, std::size_t size) {
    if (!pointer || size == 0) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(pointer, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD)) return false;
    const DWORD access = info.Protect & 0xFF;
    if (access == PAGE_NOACCESS || access == PAGE_EXECUTE) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(pointer);
    const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    return start + size >= start && start + size <= end;
}

bool sequence_matches(const SequenceAsset& asset, const std::uint8_t* live) {
    constexpr std::size_t probe = 32;
    if (asset.bytes.size() >= probe && readable(live, probe) &&
        std::memcmp(live, asset.bytes.data(), probe) == 0) return true;
    return asset.bytes.size() >= 0x24 + probe && readable(live, probe) &&
           std::memcmp(live, asset.bytes.data() + 0x24, probe) == 0;
}

std::uint64_t load_serial_for(const std::string& container) {
    std::uint64_t result = 0;
    EnterCriticalSection(&g_provenance_lock);
    for (const auto& item : g_load_provenance)
        if (item.container == container) result = std::max(result, item.serial);
    LeaveCriticalSection(&g_provenance_lock);
    return result;
}

ContainerResolution resolve_container(const std::uint8_t* inverse_map,
                                      int map_selector, int bank_id,
                                      const void* sequence_table) {
    ContainerResolution result;
    if (!inverse_map || map_selector < 0) return result;
    const auto map_address = reinterpret_cast<std::uintptr_t>(inverse_map);
    const auto displacement = static_cast<std::uintptr_t>(map_selector) * 128;
    if (map_address < displacement) return result;
    const auto* live_maps = reinterpret_cast<const std::uint8_t*>(map_address - displacement);
    const auto* live_sequence = reinterpret_cast<const std::uint8_t*>(sequence_table);

    std::vector<const ContainerIdentity*> identities;
    std::vector<const SequenceAsset*> sequences;
    std::vector<music_identity::Candidate> candidates;
    for (const auto& identity : g_containers) {
        if (identity.bank_id != bank_id ||
            map_selector >= static_cast<int>(identity.maps.size())) continue;
        const auto map_bytes = identity.maps.size() * sizeof(identity.maps[0]);
        if (!readable(live_maps, map_bytes) ||
            std::memcmp(live_maps, identity.maps.data(), map_bytes) != 0) continue;

        const SequenceAsset* matching_sequence = nullptr;
        if (live_sequence) {
            for (const auto& asset : g_sequences) {
                if (asset.container == identity.container && sequence_matches(asset, live_sequence)) {
                    matching_sequence = &asset;
                    break;
                }
            }
        }
        identities.push_back(&identity);
        sequences.push_back(matching_sequence);
        candidates.push_back({identity.container, matching_sequence != nullptr,
                              load_serial_for(identity.container)});
    }

    result.candidate_count = static_cast<int>(candidates.size());
    const auto resolved = music_identity::resolve(candidates);
    result.evidence = resolved.evidence;
    if (resolved.index < 0) return result;
    const auto index = static_cast<std::size_t>(resolved.index);
    result.identity = identities[index];
    result.sequence = sequences[index];
    result.load_serial = candidates[index].load_serial;
    return result;
}

bool known_container(const std::string& container) {
    for (const auto& identity : g_containers)
        if (identity.container == container) return true;
    return false;
}

void record_music_load(const char* path) {
    if (!path) return;
    const auto container = music_identity::container_from_path(path);
    if (!container || !known_container(*container)) return;
    std::uint64_t serial = 0;
    EnterCriticalSection(&g_provenance_lock);
    serial = ++g_load_serial;
    g_load_provenance.push_back({*container, path, serial});
    if (g_load_provenance.size() > 256) g_load_provenance.erase(g_load_provenance.begin());
    LeaveCriticalSection(&g_provenance_lock);
    log_line("music load serial=%llu container=%s path=%s",
             static_cast<unsigned long long>(serial), container->c_str(), path);
}

HANDLE WINAPI hook_create_file_a(LPCSTR path, DWORD access, DWORD sharing,
                                 LPSECURITY_ATTRIBUTES security, DWORD creation,
                                 DWORD flags, HANDLE template_file) {
    const auto result = g_create_file_a(path, access, sharing, security, creation, flags, template_file);
    if (result != INVALID_HANDLE_VALUE) record_music_load(path);
    return result;
}

void** find_main_import(const char* dll_name, const char* function_name) {
    auto* image = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (!image || dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return nullptr;
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return nullptr;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        const auto* imported_dll = reinterpret_cast<const char*>(image + descriptor->Name);
        if (_stricmp(imported_dll, dll_name) != 0) continue;
        if (!descriptor->OriginalFirstThunk || !descriptor->FirstThunk) return nullptr;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(image + descriptor->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA32*>(image + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) continue;
            const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(image + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), function_name) == 0)
                return reinterpret_cast<void**>(&addresses->u1.Function);
        }
    }
    return nullptr;
}

bool install_file_provenance_hook() {
    auto** slot = find_main_import("KERNEL32.dll", "CreateFileA");
    if (!slot) {
        log_line("music load provenance hook rejected: CreateFileA import unavailable");
        return false;
    }
    g_create_file_a = reinterpret_cast<CreateFileAFn>(*slot);
    DWORD old_protection = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old_protection)) return false;
    *slot = reinterpret_cast<void*>(&hook_create_file_a);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), old_protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    log_line("music load provenance hook installed at %p", slot);
    return true;
}

std::int32_t WINAPI hook_miles_wave_out_open(aitd4::MilesDriver* driver,
                                             LPHWAVEOUT* wave_out,
                                             std::int32_t device_id,
                                             LPWAVEFORMAT format) {
    const auto result = g_miles_wave_out_open(driver, wave_out, device_id, format);
    const auto captured = driver ? *driver : nullptr;
    log_line("Miles waveOutOpen result=%ld driver=%p rate=%lu channels=%u",
             static_cast<long>(result), captured,
             format ? static_cast<unsigned long>(format->nSamplesPerSec) : 0,
             format ? format->nChannels : 0);
    if (result == 0 && captured && g_miles_stream && !g_miles_stream->running()) {
        attach_miles_stream(captured, "waveOutOpen");
    }
    return result;
}

aitd4::MilesSample WINAPI hook_miles_allocate_sample(aitd4::MilesDriver driver) {
    const auto sample = g_miles_allocate_sample(driver);
    attach_miles_stream(driver, "allocate_sample_handle");
    return sample;
}

bool install_miles_output_hook() {
    auto module = GetModuleHandleA("mss32.dll");
    aitd4::MilesApi api{};
    if (!module || !aitd4::resolve_miles_api(module, api)) {
        log_line("Miles AICA stream rejected: required Miles 6.1 exports unavailable");
        return false;
    }
    auto** wave_slot = find_main_import("Mss32.dll", "_AIL_waveOutOpen@16");
    auto** allocate_slot = find_main_import("Mss32.dll", "_AIL_allocate_sample_handle@4");
    if (!wave_slot || !allocate_slot) {
        log_line("Miles AICA stream rejected: required game imports unavailable wave=%p allocate=%p",
                 wave_slot, allocate_slot);
        return false;
    }
    g_miles_wave_out_open = reinterpret_cast<MilesWaveOutOpenFn>(*wave_slot);
    g_miles_allocate_sample = reinterpret_cast<MilesAllocateSampleFn>(*allocate_slot);
    g_miles_stream = std::make_unique<aitd4::MilesStream>(api, &aitd4::renderer_render_pcm,
                                                          &stream_log);
    DWORD wave_protection = 0;
    if (!VirtualProtect(wave_slot, sizeof(*wave_slot), PAGE_READWRITE, &wave_protection)) return false;
    *wave_slot = reinterpret_cast<void*>(&hook_miles_wave_out_open);
    DWORD ignored = 0;
    VirtualProtect(wave_slot, sizeof(*wave_slot), wave_protection, &ignored);
    DWORD allocate_protection = 0;
    if (!VirtualProtect(allocate_slot, sizeof(*allocate_slot), PAGE_READWRITE,
                        &allocate_protection)) return false;
    *allocate_slot = reinterpret_cast<void*>(&hook_miles_allocate_sample);
    VirtualProtect(allocate_slot, sizeof(*allocate_slot), allocate_protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), wave_slot, sizeof(*wave_slot));
    FlushInstructionCache(GetCurrentProcess(), allocate_slot, sizeof(*allocate_slot));
    log_line("Miles driver hooks installed waveOutOpen=%p allocate_sample=%p",
             wave_slot, allocate_slot);
    return true;
}

int player_index_from_handle(void* handle, std::uint8_t* image_base) {
    if (!g_profile) return -1;
    auto handles = reinterpret_cast<void**>(image_base + g_profile->handles_rva);
    for (int index = 0; index < sequence_slots; ++index) if (handles[index] == handle) return index;
    return -1;
}

using DispatchFn = int(__cdecl*)(void*, std::uint8_t*);

bool bind_player(int player, const std::uint8_t* inverse_map, int map_selector,
                 int bank_id, const void* sequence_table, const void* sequence_base,
                 const void* sequence_cursor, bool log_failure) {
    if (!aitd4::renderer_ready()) {
        if (log_failure)
            log_line("scene slot=%d bank_id=%d selector=%d map=%p seqtable=%p seqbase=%p cursor=%p "
                     "match=deferred/- evidence=renderer-not-ready candidates=0 bound=no",
                     player, bank_id, map_selector, inverse_map, sequence_table,
                     sequence_base, sequence_cursor);
        return false;
    }

    if (!sequence_table) {
        const bool bound = aitd4::renderer_set_scene(player, "gamesnd", 5);
        if (bound) g_last_container[player] = "gamesnd";
        if (log_failure || bound)
            log_line("scene slot=%d bank_id=%d selector=%d map=%p seqtable=%p seqbase=%p cursor=%p "
                     "match=gamesnd/- evidence=no-sequence candidates=1 bound=%s",
                     player, bank_id, map_selector, inverse_map, sequence_table,
                     sequence_base, sequence_cursor, bound ? "yes" : "no");
        return bound;
    }

    const auto resolution = resolve_container(inverse_map, map_selector, bank_id, sequence_table);
    if (!resolution.identity) {
        if (log_failure)
            log_line("scene slot=%d bank_id=%d selector=%d map=%p seqtable=%p seqbase=%p cursor=%p "
                     "match=unresolved/- evidence=%s candidates=%d bound=no",
                     player, bank_id, map_selector, inverse_map, sequence_table,
                     sequence_base, sequence_cursor,
                     music_identity::evidence_name(resolution.evidence), resolution.candidate_count);
        return false;
    }

    const bool bound = aitd4::renderer_set_scene(
        player, resolution.identity->container.c_str(), resolution.identity->bank_id);
    if (bound) g_last_container[player] = resolution.identity->container;
    if (log_failure || bound)
        log_line("scene slot=%d bank_id=%d selector=%d map=%p seqtable=%p seqbase=%p cursor=%p "
                 "match=%s/%s evidence=%s candidates=%d load_serial=%llu bound=%s",
                 player, bank_id, map_selector, inverse_map, sequence_table,
                 sequence_base, sequence_cursor, resolution.identity->container.c_str(),
                 resolution.sequence ? resolution.sequence->name.c_str() : "-",
                 music_identity::evidence_name(resolution.evidence), resolution.candidate_count,
                 static_cast<unsigned long long>(resolution.load_serial), bound ? "yes" : "no");
    return bound;
}

int __cdecl hook_dispatch(void* handle, std::uint8_t* message) {
    auto image_base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    const int player = player_index_from_handle(handle, image_base);
    int map_selector = -1;
    int bank_id = -1;
    std::uint8_t* inverse_map = nullptr;
    void* sequence_table = nullptr;
    void* sequence_base = nullptr;
    void* sequence_cursor = nullptr;
    if (player >= 0) {
        // alone4.exe has five sequence-state records. 0x5220EC is merely
        // record 0's active flag (state+0x134), not the array base.
        auto state = image_base + g_profile->sequence_state_rva + player * 0x144;
        map_selector = state[0x136];
        bank_id = state[0x137];
        if (bank_id >= 0 && bank_id < 6 && map_selector >= 0 && map_selector < 6)
            inverse_map = *reinterpret_cast<std::uint8_t**>(image_base + g_profile->inverse_maps_rva + bank_id * 0x18 + map_selector * 4);
        sequence_table = *reinterpret_cast<void**>(state + 0x120);
        sequence_base = *reinterpret_cast<void**>(state + 0x124);
        sequence_cursor = *reinterpret_cast<void**>(state + 0x12C);
    }

    // Non-persistent handles are the game's short MIDI SFX. They remain on
    // the original PC dispatcher and never enter the Dreamcast renderer.
    if (player < 0)
        return reinterpret_cast<DispatchFn>(g_dispatch_trampoline)(handle, message);

    const std::uint8_t status = message ? message[0] : 0;
    const std::uint8_t kind = status & 0xF0;
    const std::uint8_t channel = status & 0x0F;
    const bool state_changed = sequence_table != g_last_sequence_table[player] ||
                               inverse_map != g_last_inverse_map[player] ||
                               bank_id != g_last_bank_id[player] ||
                               map_selector != g_last_map_selector[player];
    if (state_changed) {
        // A Manatee player survives bank selection. Stop every outstanding
        // note before replacing its scene so no prior envelope can become the
        // continuous tone reported after a transition.
        aitd4::renderer_stop_player(player);
        g_channel_program[player].fill(0xFF);
        g_renderer_bound[player] = false;
        g_last_container[player].clear();
        g_last_sequence_table[player] = sequence_table;
        g_last_inverse_map[player] = inverse_map;
        g_last_bank_id[player] = bank_id;
        g_last_map_selector[player] = map_selector;
    }

    if (!g_renderer_bound[player]) {
        g_renderer_bound[player] = bind_player(
            player, inverse_map, map_selector, bank_id, sequence_table,
            sequence_base, sequence_cursor, state_changed);
    }

    // Suppress every persistent-player event unless the exact Dreamcast bank
    // is bound. In particular, never reinterpret unresolved scene music with
    // gamesnd: that is a proven wrong-bank/runaway-tone failure path.
    if (!message || !g_renderer_bound[player] || kind < 0x80 || kind > 0xE0)
        return 0;

    // The PC DSEQ parser has already applied its inverse program map before it
    // calls the generic MIDI dispatch (see alone4.exe 0x498F3C..0x498F89).
    // Therefore message[1] on a C0 event is the final local bank program. Do
    // not translate it a second time here.
    if (kind == 0xC0) g_channel_program[player][channel] = message[1];
    const auto remembered_program = g_channel_program[player][channel];
    const int local_program = remembered_program == 0xFF ? -1 : remembered_program;

    if (kind == 0xC0)
        log_line("program slot=%d container=%s local=%u bank_id=%d selector=%d map=%p",
                 player, g_last_container[player].c_str(), message[1],
                 bank_id, map_selector, inverse_map);
    log_line("event slot=%d container=%s status=%02X data1=%02X data2=%02X program=%d",
             player, g_last_container[player].c_str(), status, message[1],
             (kind == 0xC0 || kind == 0xD0) ? 0 : message[2], local_program);

    std::uint8_t normalized[3]{status, message[1],
        (kind == 0xC0 || kind == 0xD0) ? std::uint8_t{0} : message[2]};
    aitd4::renderer_event(player, g_last_container[player].c_str(), local_program, normalized);
    return 0;
}

DWORD WINAPI renderer_thread(void*) {
    const bool ok = aitd4::renderer_start(g_asset_root, g_log_path);
    log_line("Dreamcast renderer %s; music slots suppressed, PC SFX handles preserved",
             ok ? "ready" : "failed");
    EnterCriticalSection(&g_log_lock); if (g_log) std::fflush(g_log); LeaveCriticalSection(&g_log_lock);
    return ok ? 0 : 1;
}

bool install_dispatch_hook() {
    auto image_base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    const std::uint8_t expected[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x4C};
    std::uint8_t* target = nullptr;
    for (const auto& profile : executable_profiles) {
        auto candidate = image_base + profile.dispatch_rva;
        if (std::memcmp(candidate, expected, sizeof(expected)) == 0) {
            if (target) {
                log_line("dispatch hook rejected: executable matches more than one profile");
                return false;
            }
            target = candidate;
            g_profile = &profile;
        }
    }
    if (!target || !g_profile) {
        log_line("dispatch hook rejected: unsupported alone4.exe");
        return false;
    }
    auto trampoline = reinterpret_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) return false;
    std::memcpy(trampoline, target, sizeof(expected));
    trampoline[6] = 0xE9;
    *reinterpret_cast<std::int32_t*>(trampoline + 7) =
        static_cast<std::int32_t>((target + 6) - (trampoline + 11));
    g_dispatch_trampoline = trampoline;
    DWORD old_protection = 0;
    if (!VirtualProtect(target, sizeof(expected), PAGE_EXECUTE_READWRITE, &old_protection)) return false;
    target[0] = 0xE9;
    *reinterpret_cast<std::int32_t*>(target + 1) = static_cast<std::int32_t>(reinterpret_cast<std::uint8_t*>(&hook_dispatch) - (target + 5));
    target[5] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(expected));
    DWORD ignored = 0; VirtualProtect(target, sizeof(expected), old_protection, &ignored);
    log_line("dispatch hook installed at %p profile=%s", target, g_profile->name);
    return true;
}

void silence_existing_pc_music() {
    if (!g_profile || !g_dispatch_trampoline) return;
    auto* image_base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    auto** handles = reinterpret_cast<void**>(image_base + g_profile->handles_rva);
    unsigned silenced = 0;
    for (int player = 0; player != sequence_slots; ++player) {
        if (!handles[player]) continue;
        for (std::uint8_t channel = 0; channel != 16; ++channel) {
            std::uint8_t sustain[]{std::uint8_t(0xB0 | channel), 64, 0};
            std::uint8_t all_sound_off[]{std::uint8_t(0xB0 | channel), 120, 0};
            std::uint8_t all_notes_off[]{std::uint8_t(0xB0 | channel), 123, 0};
            reinterpret_cast<DispatchFn>(g_dispatch_trampoline)(handles[player], sustain);
            reinterpret_cast<DispatchFn>(g_dispatch_trampoline)(handles[player], all_sound_off);
            reinterpret_cast<DispatchFn>(g_dispatch_trampoline)(handles[player], all_notes_off);
        }
        ++silenced;
    }
    log_line("persistent PC music shutdown handles=%u; no PC music fallback", silenced);
}

bool initialize_impl(HMODULE self) {
    InitializeCriticalSection(&g_log_lock);
    InitializeCriticalSection(&g_provenance_lock);
    for (auto& programs : g_channel_program) programs.fill(0xFF);
    char exe_path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    char* slash = std::strrchr(exe_path, '\\');
    if (!slash) return false;
    *slash = '\0';
    std::snprintf(g_log_path, MAX_PATH, "%s\\audio-restoration\\aitd4-audio-hook.log", exe_path);
    g_log = _fsopen(g_log_path, "w", _SH_DENYNO);
    if (g_log) setvbuf(g_log, nullptr, _IOFBF, 64 * 1024);
    const bool assets_found = locate_assets(self);
    const bool catalog_loaded = assets_found && load_sequence_catalog();
    log_line("audio hook loaded image=%p assets=%s sequences=%zu", GetModuleHandleW(nullptr),
             assets_found ? g_asset_root : "missing", g_sequences.size());
    if (!catalog_loaded) log_line("sequence catalog unavailable");
    if (!install_miles_output_hook()) return false;
    if (!install_dispatch_hook()) return false;
    silence_existing_pc_music();
    if (!install_file_provenance_hook()) return false;
    HANDLE thread = CreateThread(nullptr, 0, renderer_thread, nullptr, 0, nullptr);
    if (!thread) { log_line("renderer thread creation failed"); return false; }
    const DWORD wait = WaitForSingleObject(thread, INFINITE);
    DWORD renderer_exit = 1;
    const bool renderer_started = wait == WAIT_OBJECT_0 &&
                                  GetExitCodeThread(thread, &renderer_exit) &&
                                  renderer_exit == 0;
    CloseHandle(thread);
    if (!renderer_started) {
        log_line("renderer initialization did not complete successfully wait=%lu exit=%lu",
                 wait, renderer_exit);
        return false;
    }
    return true;
}

}  // namespace

extern "C" DWORD WINAPI AITD4_AudioInitialize(void* reserved) {
    (void)reserved;
    return g_initializer.run([] {
        return g_self != nullptr && initialize_impl(g_self);
    });
}

BOOL WINAPI DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = self;
        DisableThreadLibraryCalls(self);
    }
    return TRUE;
}
