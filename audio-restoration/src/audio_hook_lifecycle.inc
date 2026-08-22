// Renderer thread startup, hook installation and DLL initialization.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

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
    InitializeCriticalSection(&g_identity_lock);
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
    if (!install_container_identity_hook()) return false;
    silence_existing_pc_music();
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
