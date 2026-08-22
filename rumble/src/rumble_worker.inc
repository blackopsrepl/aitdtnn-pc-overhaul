// Controller selection, timed motor worker and Dreamcast command translation.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

bool game_has_foreground() noexcept {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) return false;
    DWORD process = 0;
    GetWindowThreadProcessId(foreground, &process);
    return process == GetCurrentProcessId();
}

int find_controller() noexcept {
    if (g_xinput_get_state == nullptr) return -1;
    XINPUT_STATE state{};
    if (g_requested_controller >= 0) {
        return g_xinput_get_state(static_cast<DWORD>(g_requested_controller), &state) ==
            ERROR_SUCCESS ? g_requested_controller : -1;
    }
    for (DWORD index = 0; index != XUSER_MAX_COUNT; ++index) {
        if (g_xinput_get_state(index, &state) == ERROR_SUCCESS)
            return static_cast<int>(index);
    }
    return -1;
}

void set_controller_output(int controller, WORD motor) noexcept {
    if (controller < 0 || g_xinput_set_state == nullptr) return;
    XINPUT_VIBRATION vibration{};
    vibration.wLeftMotorSpeed = motor;
    vibration.wRightMotorSpeed = motor;
    const DWORD result = g_xinput_set_state(static_cast<DWORD>(controller), &vibration);
    if (result != ERROR_SUCCESS) {
        log_line("XInputSetState controller=%d motor=%u result=%lu", controller,
                 static_cast<unsigned>(motor), static_cast<unsigned long>(result));
    }
}

DWORD WINAPI rumble_worker(void*) noexcept {
    int controller = find_controller();
    WORD last_motor = 0xffffu;
    InterlockedExchange(&g_connected_controller, controller);
    if (controller >= 0) log_line("controller selected index=%d", controller);
    log_line("scheduler ready tick_ms=8");
    SetEvent(g_ready_event);
    for (;;) {
        if (WaitForSingleObject(g_stop_event, 8) == WAIT_OBJECT_0) break;

        const ULONGLONG now = GetTickCount64();
        std::uint32_t raw = kStopVibset;
        AcquireSRWLockExclusive(&g_command_lock);
        if (!game_has_foreground()) {
            g_raw_vibset = kStopVibset;
            g_command_deadline = 0;
        } else if (g_command_deadline != 0 && now >= g_command_deadline) {
            g_raw_vibset = kStopVibset;
            g_command_deadline = 0;
        }
        raw = g_raw_vibset;
        ReleaseSRWLockExclusive(&g_command_lock);

        XINPUT_STATE state{};
        if (controller >= 0 &&
            g_xinput_get_state(static_cast<DWORD>(controller), &state) != ERROR_SUCCESS) {
            log_line("controller disconnected index=%d", controller);
            controller = -1;
            InterlockedExchange(&g_connected_controller, -1);
            last_motor = 0xffffu;
        }
        if (controller < 0) {
            controller = find_controller();
            if (controller >= 0) {
                log_line("controller selected index=%d", controller);
                InterlockedExchange(&g_connected_controller, controller);
                last_motor = 0xffffu;
            }
        }

        const auto decoded = decode_vibset(raw);
        const WORD motor = to_xinput_motor(decoded.power, g_strength);
        if (controller >= 0 && motor != last_motor) {
            set_controller_output(controller, motor);
            log_line("output controller=%d raw=%08X power=%.6f motor=%u", controller,
                     raw, decoded.power, static_cast<unsigned>(motor));
            last_motor = motor;
        }
    }
    if (controller >= 0) set_controller_output(controller, 0);
    InterlockedExchange(&g_connected_controller, -1);
    log_line("scheduler stopped");
    return 0;
}

void submit_request(std::uint32_t selector, std::uint32_t value) noexcept {
    const std::uint32_t normalized_selector = selector == 0 ? 0u : 1u;
    const std::uint32_t raw = request_to_vibset(normalized_selector, value);
    bool changed = false;
    AcquireSRWLockExclusive(&g_command_lock);
    changed = g_selector_values[normalized_selector] != value;
    g_selector_values[normalized_selector] = value;
    g_raw_vibset = raw;
    g_command_deadline = value == 0
        ? 0
        : GetTickCount64() + kDreamcastAutoStopMilliseconds;
    ReleaseSRWLockExclusive(&g_command_lock);
    if (changed) {
        const auto decoded = decode_vibset(raw);
        log_line("request source=pc-backend selector=%u value=%u raw=%08X power=%.6f "
                 "duration_ms=%u", normalized_selector, value, raw, decoded.power,
                 decoded.duration_milliseconds);
    }
}

void __fastcall hooked_set_vibration_enabled(void*, void*,
                                              std::uint32_t enabled) noexcept {
    const LONG normalized = enabled != 0 ? 1 : 0;
    const LONG previous = InterlockedExchange(&g_game_vibration_enabled, normalized);
    if (previous != normalized) log_line("game vibration enabled=%ld", normalized);
    if (normalized == 0) {
        submit_request(0, 0);
        submit_request(1, 0);
    }
}

unsigned char __fastcall hooked_rumble_backend(void*, void*, std::uint32_t selector,
                                                std::uint32_t value) noexcept {
    if (!g_enabled ||
        InterlockedCompareExchange(&g_game_vibration_enabled, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_connected_controller, 0, 0) < 0)
        return 0;
    submit_request(selector, value);
    return 1;
}

unsigned char __fastcall hooked_rumble_available(void*, void*) noexcept {
    return g_enabled &&
            InterlockedCompareExchange(&g_connected_controller, 0, 0) >= 0
        ? 1u : 0u;
}

bool write_memory(void* target, const void* bytes, std::size_t size) noexcept {
    DWORD old_protection = 0;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &old_protection)) return false;
    std::memcpy(target, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    DWORD ignored = 0;
    return VirtualProtect(target, size, old_protection, &ignored) != FALSE;
}

void** find_import_slot(const char* library, const char* function) noexcept {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (base == nullptr) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    const auto& directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0) return nullptr;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        base + directory.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor) {
        const char* imported_library = reinterpret_cast<const char*>(base + descriptor->Name);
        if (_stricmp(imported_library, library) != 0) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(
            base + descriptor->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA32*>(
            base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) continue;
            const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), function) == 0)
                return reinterpret_cast<void**>(&addresses->u1.Function);
        }
    }
    return nullptr;
}

bool make_relative_jump(unsigned char* target, void* replacement,
                        unsigned char patch[5]) noexcept {
    const std::intptr_t displacement =
        reinterpret_cast<unsigned char*>(replacement) - (target + 5);
    if (displacement < INT32_MIN || displacement > INT32_MAX) return false;
    patch[0] = 0xe9;
    const auto relative = static_cast<std::int32_t>(displacement);
    std::memcpy(patch + 1, &relative, sizeof(relative));
    return true;
}
