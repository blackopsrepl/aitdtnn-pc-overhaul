// Game-code hooks, rollback, explicit initialization and DLL entry point.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

bool install_backend_hooks() noexcept {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (base == nullptr) return false;
    if (g_profile == nullptr) return false;
    g_enable_target = base + g_profile->enable_rva;
    g_backend_target = base + g_profile->backend_rva;
    g_available_target = base + g_profile->available_rva;
    if (std::memcmp(g_enable_target, kExpectedEnable, sizeof(kExpectedEnable)) != 0 ||
        std::memcmp(g_backend_target, kExpectedBackend, sizeof(kExpectedBackend)) != 0 ||
        std::memcmp(g_available_target, kExpectedAvailable,
                    sizeof(kExpectedAvailable)) != 0) {
        log_line("rumble platform-method signature mismatch enable=%08X set=%08X "
                 "available=%08X", g_profile->enable_rva, g_profile->backend_rva,
                 g_profile->available_rva);
        return false;
    }

    unsigned char enable_patch[5]{};
    unsigned char backend_patch[5]{};
    unsigned char available_patch[5]{};
    if (!make_relative_jump(g_enable_target,
                            reinterpret_cast<void*>(&hooked_set_vibration_enabled),
                            enable_patch) ||
        !make_relative_jump(g_backend_target,
                            reinterpret_cast<void*>(&hooked_rumble_backend),
                            backend_patch) ||
        !make_relative_jump(g_available_target,
                            reinterpret_cast<void*>(&hooked_rumble_available),
                            available_patch)) {
        return false;
    }
    if (!write_memory(g_enable_target, enable_patch, sizeof(enable_patch))) return false;
    if (!write_memory(g_backend_target, backend_patch, sizeof(backend_patch))) {
        write_memory(g_enable_target, kExpectedEnable, 5);
        return false;
    }
    if (!write_memory(g_available_target, available_patch, sizeof(available_patch))) {
        write_memory(g_backend_target, kExpectedBackend, 5);
        write_memory(g_enable_target, kExpectedEnable, 5);
        return false;
    }
    log_line("hooked PC vibration platform methods enable=%08X set=%08X "
             "available=%08X", g_profile->enable_rva, g_profile->backend_rva,
             g_profile->available_rva);
    return true;
}

void rollback_backend_hooks() noexcept {
    if (g_available_target != nullptr)
        write_memory(g_available_target, kExpectedAvailable, 5);
    if (g_backend_target != nullptr) write_memory(g_backend_target, kExpectedBackend, 5);
    if (g_enable_target != nullptr) write_memory(g_enable_target, kExpectedEnable, 5);
}

void shutdown_rumble() noexcept {
    if (InterlockedCompareExchange(&g_shutdown_started, 1, 0) != 0) return;
    if (g_stop_event != nullptr) SetEvent(g_stop_event);
    if (g_worker_thread != nullptr) {
        const DWORD wait = WaitForSingleObject(g_worker_thread, 1000);
        if (wait != WAIT_OBJECT_0) {
            const int controller = find_controller();
            if (controller >= 0) set_controller_output(controller, 0);
        }
    }
}

__declspec(noreturn) void WINAPI hooked_exit_process(UINT code) noexcept {
    shutdown_rumble();
    g_real_exit_process(code);
    for (;;) Sleep(INFINITE);
}

bool install_exit_hook() noexcept {
    g_exit_process_slot = find_import_slot("KERNEL32.dll", "ExitProcess");
    if (g_exit_process_slot == nullptr || *g_exit_process_slot == nullptr) return false;
    g_real_exit_process = reinterpret_cast<ExitProcessFn>(*g_exit_process_slot);
    void* replacement = reinterpret_cast<void*>(&hooked_exit_process);
    if (!write_memory(g_exit_process_slot, &replacement, sizeof(replacement))) return false;
    log_line("hooked KERNEL32!ExitProcess for motor shutdown");
    return true;
}

bool initialize_impl() noexcept {
    wchar_t directory[32768]{};
    if (!module_directory(directory, std::size(directory))) return false;
    wcscpy_s(g_log_path, directory);
    if (!append_path(g_log_path, std::size(g_log_path), kLogName)) return false;
    log_line("initializing AITD:TNN Dreamcast rumble restoration");

    if (!validate_executable()) return false;
    load_config();
    log_line("config enabled=%u controller=%d strength=%.3f profile=Dreamcast",
             g_enabled ? 1u : 0u, g_requested_controller, g_strength);
    if (!g_enabled) {
        log_line("rumble disabled by configuration");
        return true;
    }
    if (!load_xinput()) {
        log_line("no usable system XInput DLL was found");
        return false;
    }
    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_ready_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stop_event == nullptr || g_ready_event == nullptr) return false;
    g_worker_thread = CreateThread(nullptr, 0, rumble_worker, nullptr, 0, nullptr);
    if (g_worker_thread == nullptr) return false;
    if (WaitForSingleObject(g_ready_event, 5000) != WAIT_OBJECT_0) {
        log_line("scheduler readiness timeout");
        shutdown_rumble();
        return false;
    }

    if (!install_backend_hooks()) {
        shutdown_rumble();
        return false;
    }
    if (!install_exit_hook()) {
        rollback_backend_hooks();
        shutdown_rumble();
        return false;
    }
    log_line("rumble restoration initialized");
    return true;
}

}  // namespace

extern "C" DWORD WINAPI AITD4_RumbleInitialize(void*) {
    AcquireSRWLockExclusive(&g_initialize_lock);
    while (g_initialize_state == InitializeState::running) {
        SleepConditionVariableSRW(&g_initialize_changed, &g_initialize_lock, INFINITE, 0);
    }
    if (g_initialize_state == InitializeState::succeeded) {
        ReleaseSRWLockExclusive(&g_initialize_lock);
        return 1;
    }
    if (g_initialize_state == InitializeState::failed) {
        ReleaseSRWLockExclusive(&g_initialize_lock);
        return 0;
    }
    g_initialize_state = InitializeState::running;
    ReleaseSRWLockExclusive(&g_initialize_lock);

    const bool ok = initialize_impl();
    AcquireSRWLockExclusive(&g_initialize_lock);
    g_initialize_state = ok ? InitializeState::succeeded : InitializeState::failed;
    WakeAllConditionVariable(&g_initialize_changed);
    ReleaseSRWLockExclusive(&g_initialize_lock);
    return ok ? 1u : 0u;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
