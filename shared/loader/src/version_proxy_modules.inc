// Module initialization and the guarded game-entrypoint handoff.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

using InitializeModule = DWORD(WINAPI*)(void*);

void load_and_initialize_module(const wchar_t* relative_path,
                                const char* export_name,
                                HMODULE* module_out) noexcept {
    wchar_t path[32768]{};
    if (!module_path(relative_path, path, std::size(path))) {
        fail_closed("Could not construct an absolute overhaul module path.");
    }

    HMODULE module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr) {
        char message[512]{};
        sprintf_s(message, "Could not load required module: %s", export_name);
        fail_closed(message, GetLastError());
    }
    const auto initialize = reinterpret_cast<InitializeModule>(
        GetProcAddress(module, export_name));
    if (initialize == nullptr) {
        char message[512]{};
        sprintf_s(message, "Required initialization export is missing: %s", export_name);
        fail_closed(message, GetLastError());
    }
    if (initialize(nullptr) == 0) {
        char message[512]{};
        sprintf_s(message, "Module initialization failed: %s", export_name);
        fail_closed(message);
    }
    *module_out = module;
}

extern "C" void RunEntrypointGate() noexcept {
    append_log("Entrypoint gate reached; validating pristine alone4.exe.");

    wchar_t exe_path[32768]{};
    if (!executable_path(exe_path, static_cast<DWORD>(std::size(exe_path)))) {
        fail_closed("Could not resolve the running alone4.exe path.", GetLastError());
    }
    unsigned char actual_digest[32]{};
    DWORD hash_error = ERROR_SUCCESS;
    if (!hash_file(exe_path, actual_digest, &hash_error)) {
        fail_closed("Could not hash the running alone4.exe.", hash_error);
    }
    if (std::memcmp(actual_digest, kSupported15SlotExeSha256, sizeof(actual_digest)) != 0 &&
        std::memcmp(actual_digest, kSupportedRetailExeSha256, sizeof(actual_digest)) != 0) {
        char actual_hex[65]{};
        char message[256]{};
        digest_to_hex(actual_digest, actual_hex);
        sprintf_s(
            message,
            "Unsupported alone4.exe SHA-256: %s. Expected "
            "5668118e0e19d569986500a1c805a85397c8681e7b672b49a68645462eccc672 or "
            "320908af4ce5c724b60a7eea6a5aade737d51d65aee8506744fce6e6dd0143e0.",
            actual_hex);
        fail_closed(message);
    }

    load_and_initialize_module(kAudioRelativePath, "AITD4_AudioInitialize",
                               &g_audio_module);
    load_and_initialize_module(kRendererRelativePath, "AITD4_Initialize",
                               &g_renderer_module);
    load_and_initialize_module(kRumbleRelativePath, "AITD4_RumbleInitialize",
                               &g_rumble_module);
    append_log(
        "Audio restoration, renderer, and rumble initialized; entering the original game.");
}

extern "C" __declspec(naked) void EntrypointGate() {
    __asm {
        pushfd
        pushad
        call RunEntrypointGate
        popad
        popfd
        push ebp
        mov ebp, esp
        push -1
        jmp dword ptr [g_entrypoint_continue]
    }
}

bool install_entrypoint_gate(const char** failure) noexcept {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (base == nullptr) {
        *failure = "Could not locate the game image while installing the entrypoint gate.";
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        *failure = "The game image has an invalid DOS header.";
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        *failure = "The game image is not a supported 32-bit PE image.";
        return false;
    }
    const DWORD entry_rva = nt->OptionalHeader.AddressOfEntryPoint;
    if (nt->OptionalHeader.SizeOfImage < sizeof(kSupportedEntrypointBytes) ||
        entry_rva > nt->OptionalHeader.SizeOfImage - sizeof(kSupportedEntrypointBytes)) {
        *failure = "The game entrypoint lies outside the loaded image.";
        return false;
    }
    auto* entrypoint = base + entry_rva;
    if (std::memcmp(entrypoint, kSupportedEntrypointBytes,
                    sizeof(kSupportedEntrypointBytes)) != 0) {
        *failure =
            "Unsupported alone4.exe entrypoint prologue; expected bytes 55 8B EC 6A FF.";
        return false;
    }

    const std::intptr_t displacement =
        reinterpret_cast<unsigned char*>(&EntrypointGate) - (entrypoint + 5);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        *failure = "The entrypoint gate is outside the x86 relative-jump range.";
        return false;
    }
    g_entrypoint_continue = reinterpret_cast<DWORD>(entrypoint + 5);

    DWORD old_protection = 0;
    if (!VirtualProtect(entrypoint, 5, PAGE_EXECUTE_READWRITE, &old_protection)) {
        *failure = "VirtualProtect failed while installing the entrypoint gate.";
        return false;
    }
    entrypoint[0] = 0xe9;
    const std::int32_t relative = static_cast<std::int32_t>(displacement);
    std::memcpy(entrypoint + 1, &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), entrypoint, 5);
    DWORD ignored = 0;
    if (!VirtualProtect(entrypoint, 5, old_protection, &ignored)) {
        *failure = "Could not restore entrypoint memory protection after installing the gate.";
        return false;
    }
    return true;
}

}  // namespace
