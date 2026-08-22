// PC scene-container identity tracking and import-table interception.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

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

bool sequence_matches(const SequenceAsset& asset, const std::uint8_t* live_base) {
    if (asset.bytes.empty() || !readable(live_base, asset.bytes.size())) return false;
    return music_identity::loaded_dseq_matches(
        asset.bytes, std::span<const std::uint8_t>(live_base, asset.bytes.size()));
}

std::string loaded_container_for(const void* sequence_base, const void* sequence_table) {
    std::string result;
    EnterCriticalSection(&g_identity_lock);
    result = g_identity_registry.find(sequence_base, sequence_table);
    LeaveCriticalSection(&g_identity_lock);
    return result;
}

std::string loaded_bank_for(int bank_id) {
    std::string result;
    EnterCriticalSection(&g_identity_lock);
    result = g_bank_registry.find(bank_id);
    LeaveCriticalSection(&g_identity_lock);
    return result;
}

std::uint64_t loaded_bank_serial_for(int bank_id) {
    std::uint64_t result = 0;
    EnterCriticalSection(&g_identity_lock);
    result = g_bank_registry.serial(bank_id);
    LeaveCriticalSection(&g_identity_lock);
    return result;
}

const ContainerIdentity* container_identity_for(std::string_view container) {
    for (const auto& identity : g_containers)
        if (identity.container == container) return &identity;
    return nullptr;
}

ContainerResolution resolve_container(const std::uint8_t* inverse_map,
                                      int map_selector, int bank_id,
                                      const void* sequence_base,
                                      const void* sequence_table) {
    ContainerResolution result;
    const auto sequence_container = loaded_container_for(sequence_base, sequence_table);
    const auto bank_container = loaded_bank_for(bank_id);
    if (sequence_container.empty() || bank_container.empty()) {
        result.evidence = music_identity::Evidence::identity_missing;
        return result;
    }
    result.evidence = music_identity::Evidence::validation_failed;
    if (!inverse_map || map_selector < 0) return result;
    const auto map_address = reinterpret_cast<std::uintptr_t>(inverse_map);
    const auto displacement = static_cast<std::uintptr_t>(map_selector) * 128;
    if (map_address < displacement) return result;
    const auto* live_maps = reinterpret_cast<const std::uint8_t*>(map_address - displacement);
    const auto* live_sequence = reinterpret_cast<const std::uint8_t*>(sequence_base);

    result.sequence_identity = container_identity_for(sequence_container);
    result.bank_identity = container_identity_for(bank_container);
    if (!result.sequence_identity || !result.bank_identity ||
        result.bank_identity->bank_id != bank_id ||
        map_selector >= static_cast<int>(result.bank_identity->maps.size())) {
        log_line("loader identity validation failed stage=identity sequence=%s bank=%s "
                 "base=%p table=%p bank_id=%d selector=%d",
                 sequence_container.c_str(), bank_container.c_str(), sequence_base,
                 sequence_table, bank_id, map_selector);
        result.sequence_identity = nullptr;
        result.bank_identity = nullptr;
        return result;
    }

    for (const auto& asset : g_sequences) {
        if (asset.container != sequence_container ||
            !sequence_matches(asset, live_sequence)) continue;
        result.sequence = &asset;
        break;
    }
    if (!result.sequence) {
        log_line("loader identity validation failed stage=dseq sequence=%s bank=%s "
                 "base=%p table=%p bank_id=%d selector=%d",
                 sequence_container.c_str(), bank_container.c_str(), sequence_base,
                 sequence_table, bank_id, map_selector);
        result.sequence_identity = nullptr;
        result.bank_identity = nullptr;
        return result;
    }

    const auto map_bytes = result.bank_identity->maps.size() *
                           sizeof(result.bank_identity->maps[0]);
    if (!readable(live_maps, map_bytes) ||
        std::memcmp(live_maps, result.bank_identity->maps.data(), map_bytes) != 0) {
        log_line("loader identity validation failed stage=map sequence=%s bank=%s "
                 "base=%p table=%p bank_id=%d selector=%d",
                 sequence_container.c_str(), bank_container.c_str(), sequence_base,
                 sequence_table, bank_id, map_selector);
        result.sequence_identity = nullptr;
        result.bank_identity = nullptr;
        result.sequence = nullptr;
        return result;
    }

    result.evidence = music_identity::Evidence::loader_identity;
    result.candidate_count = 1;
    return result;
}

bool known_container(const std::string& container) {
    return container_identity_for(container) != nullptr;
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

void* __cdecl hook_container_loader(const char* path) {
    struct StateSnapshot { const void* table; const void* base; bool active; };
    std::array<StateSnapshot, sequence_slots> before{};
    auto* image_base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    if (g_profile) {
        for (int player = 0; player != sequence_slots; ++player) {
            auto* state = image_base + g_profile->sequence_state_rva + player * 0x144;
            before[player] = {*reinterpret_cast<void**>(state + 0x120),
                              *reinterpret_cast<void**>(state + 0x124),
                              state[0x134] != 0};
        }
    }

    const auto container = music_identity::container_from_path(path ? path : "");
    const auto result = reinterpret_cast<ContainerLoaderFn>(g_container_loader_trampoline)(path);
    if (!container || !known_container(*container) || !g_profile) return result;

    const auto* loaded_identity = container_identity_for(*container);
    std::uint64_t bank_serial = 0;
    EnterCriticalSection(&g_identity_lock);
    bank_serial = g_bank_registry.record(loaded_identity->bank_id, *container);
    LeaveCriticalSection(&g_identity_lock);
    log_line("bank identity serial=%llu bank_id=%d container=%s path=%s",
             static_cast<unsigned long long>(bank_serial), loaded_identity->bank_id,
             container->c_str(), path);

    for (int player = 0; player != sequence_slots; ++player) {
        auto* state = image_base + g_profile->sequence_state_rva + player * 0x144;
        const auto table = *reinterpret_cast<void**>(state + 0x120);
        const auto base = *reinterpret_cast<void**>(state + 0x124);
        const bool active = state[0x134] != 0;
        if (!base || !active || (before[player].active &&
            table == before[player].table && base == before[player].base)) continue;
        std::uint64_t serial = 0;
        EnterCriticalSection(&g_identity_lock);
        serial = g_identity_registry.record(base, table, *container);
        LeaveCriticalSection(&g_identity_lock);
        log_line("container identity serial=%llu player=%d container=%s base=%p table=%p path=%s",
                 static_cast<unsigned long long>(serial), player, container->c_str(),
                 base, table, path);
    }
    return result;
}

bool install_container_identity_hook() {
    if (!g_profile) return false;
    auto* image_base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    const std::uint8_t signature[] = {
        0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C, 0x56, 0x8B, 0x45, 0x08, 0x50, 0xE8};
    constexpr std::size_t stolen_size = 7;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image_base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image_base + dos->e_lfanew);
    auto* code = image_base + nt->OptionalHeader.BaseOfCode;
    const auto code_size = static_cast<std::size_t>(nt->OptionalHeader.SizeOfCode);
    std::uint8_t* target = nullptr;
    for (std::size_t at = 0; at + sizeof(signature) <= code_size; ++at) {
        if (std::memcmp(code + at, signature, sizeof(signature)) != 0) continue;
        if (target) {
            log_line("container identity hook rejected: loader signature is not unique");
            return false;
        }
        target = code + at;
    }
    if (!target) {
        log_line("container identity hook rejected: loader signature unavailable");
        return false;
    }
    auto* trampoline = reinterpret_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) return false;
    std::memcpy(trampoline, target, stolen_size);
    trampoline[7] = 0xE9;
    *reinterpret_cast<std::int32_t*>(trampoline + 8) =
        static_cast<std::int32_t>((target + 7) - (trampoline + 12));
    g_container_loader_trampoline = trampoline;
    DWORD old_protection = 0;
    if (!VirtualProtect(target, stolen_size, PAGE_EXECUTE_READWRITE, &old_protection))
        return false;
    target[0] = 0xE9;
    *reinterpret_cast<std::int32_t*>(target + 1) = static_cast<std::int32_t>(
        reinterpret_cast<std::uint8_t*>(&hook_container_loader) - (target + 5));
    target[5] = 0x90;
    target[6] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), target, stolen_size);
    DWORD ignored = 0;
    VirtualProtect(target, stolen_size, old_protection, &ignored);
    log_line("container identity hook installed at %p profile=%s", target, g_profile->name);
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
