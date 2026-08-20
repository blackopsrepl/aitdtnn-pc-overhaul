#include <windows.h>
#include <xinput.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>

#include "rumble_hook_api.hpp"
#include "rumble_protocol.hpp"
#include "executable_profile.hpp"
#include "../../shared/loader/src/sha256.hpp"

namespace {

using aitdtnn::rumble::decode_vibset;
using aitdtnn::rumble::kDreamcastAutoStopMilliseconds;
using aitdtnn::rumble::kStopVibset;
using aitdtnn::rumble::request_to_vibset;
using aitdtnn::rumble::to_xinput_motor;

using aitdtnn::rumble::kExpectedBackend;
using aitdtnn::rumble::kExpectedEnable;
using aitdtnn::rumble::kExpectedAvailable;
using aitdtnn::rumble::ExecutableProfile;
using aitdtnn::rumble::kExecutableProfiles;
constexpr wchar_t kConfigName[] = L"aitd4-rumble.ini";
constexpr wchar_t kLogName[] = L"aitd4-rumble-hook.log";

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using XInputSetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
using ExitProcessFn = VOID(WINAPI*)(UINT);

enum class InitializeState : LONG { idle, running, succeeded, failed };

HMODULE g_self = nullptr;
SRWLOCK g_initialize_lock = SRWLOCK_INIT;
CONDITION_VARIABLE g_initialize_changed = CONDITION_VARIABLE_INIT;
InitializeState g_initialize_state = InitializeState::idle;

SRWLOCK g_log_lock = SRWLOCK_INIT;
wchar_t g_log_path[32768]{};

SRWLOCK g_command_lock = SRWLOCK_INIT;
std::uint32_t g_raw_vibset = kStopVibset;
std::uint32_t g_selector_values[2]{};
ULONGLONG g_command_deadline = 0;

HANDLE g_stop_event = nullptr;
HANDLE g_ready_event = nullptr;
HANDLE g_worker_thread = nullptr;
HMODULE g_xinput_module = nullptr;
XInputGetStateFn g_xinput_get_state = nullptr;
XInputSetStateFn g_xinput_set_state = nullptr;
LONG g_shutdown_started = 0;
LONG g_connected_controller = -1;
float g_strength = 1.0f;
int g_requested_controller = -1;
bool g_enabled = true;
LONG g_game_vibration_enabled = 1;

unsigned char* g_enable_target = nullptr;
unsigned char* g_backend_target = nullptr;
unsigned char* g_available_target = nullptr;
void** g_exit_process_slot = nullptr;
ExitProcessFn g_real_exit_process = nullptr;
const ExecutableProfile* g_profile = nullptr;

bool module_directory(wchar_t* output, std::size_t capacity) noexcept {
    if (capacity == 0 || g_self == nullptr) return false;
    const DWORD length = GetModuleFileNameW(g_self, output,
                                             static_cast<DWORD>(capacity));
    if (length == 0 || length >= capacity) return false;
    wchar_t* slash = std::wcsrchr(output, L'\\');
    if (slash == nullptr) return false;
    slash[1] = L'\0';
    return true;
}

bool append_path(wchar_t* path, std::size_t capacity,
                 const wchar_t* leaf) noexcept {
    const std::size_t prefix = std::wcslen(path);
    const std::size_t suffix = std::wcslen(leaf);
    if (prefix + suffix + 1 > capacity) return false;
    wcscpy_s(path + prefix, capacity - prefix, leaf);
    return true;
}

void log_line(const char* format, ...) noexcept {
    if (g_log_path[0] == L'\0') return;
    char line[2048]{};
    va_list arguments;
    va_start(arguments, format);
    vsnprintf_s(line, sizeof(line), _TRUNCATE, format, arguments);
    va_end(arguments);

    SYSTEMTIME time{};
    GetLocalTime(&time);
    char record[2304]{};
    sprintf_s(record, "%02u:%02u:%02u.%03u %s\r\n", time.wHour, time.wMinute,
              time.wSecond, time.wMilliseconds, line);

    AcquireSRWLockExclusive(&g_log_lock);
    HANDLE file = CreateFileW(g_log_path, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, record, static_cast<DWORD>(std::strlen(record)), &written,
                  nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&g_log_lock);
}

bool executable_path(wchar_t* output, std::size_t capacity) noexcept {
    if (capacity == 0) return false;
    const DWORD length = GetModuleFileNameW(nullptr, output,
                                             static_cast<DWORD>(capacity));
    return length != 0 && length < capacity;
}

bool hash_file(const wchar_t* path, unsigned char digest[32]) noexcept {
    HANDLE file = CreateFileW(path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    aitdtnn::loader::Sha256 sha;
    unsigned char buffer[64 * 1024]{};
    bool ok = true;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) {
            ok = false;
            break;
        }
        if (read == 0) break;
        sha.update(buffer, read);
    }
    CloseHandle(file);
    if (!ok) return false;
    sha.finish(digest);
    return true;
}

bool validate_executable() noexcept {
    wchar_t path[32768]{};
    unsigned char digest[32]{};
    if (!executable_path(path, std::size(path)) || !hash_file(path, digest)) {
        log_line("could not hash the running executable error=%lu", GetLastError());
        return false;
    }
    for (const auto& profile : kExecutableProfiles) {
        if (std::memcmp(digest, profile.sha256, sizeof(digest)) == 0) {
            g_profile = &profile;
            log_line("executable profile=%s", profile.name);
            return true;
        }
    }
    log_line("unsupported alone4.exe SHA-256");
    return false;
}

void load_config() noexcept {
    wchar_t path[32768]{};
    if (!module_directory(path, std::size(path)) ||
        !append_path(path, std::size(path), kConfigName)) {
        return;
    }
    g_enabled = GetPrivateProfileIntW(L"Rumble", L"Enabled", 1, path) != 0;

    wchar_t strength[64]{};
    GetPrivateProfileStringW(L"Rumble", L"Strength", L"1.0", strength,
                             static_cast<DWORD>(std::size(strength)), path);
    wchar_t* end = nullptr;
    const double parsed = std::wcstod(strength, &end);
    if (end != strength && std::isfinite(parsed))
        g_strength = std::clamp(static_cast<float>(parsed), 0.0f, 1.0f);

    wchar_t controller[64]{};
    GetPrivateProfileStringW(L"Rumble", L"Controller", L"Auto", controller,
                             static_cast<DWORD>(std::size(controller)), path);
    if (_wcsicmp(controller, L"Auto") != 0) {
        const long parsed_controller = std::wcstol(controller, &end, 10);
        if (end != controller && parsed_controller >= 0 && parsed_controller < 4) {
            g_requested_controller = static_cast<int>(parsed_controller);
        }
    }
}

bool load_xinput() noexcept {
    wchar_t system_directory[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(system_directory,
                                             static_cast<UINT>(std::size(system_directory)));
    if (length == 0 || length >= std::size(system_directory)) return false;
    constexpr const wchar_t* candidates[] = {
        L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll",
    };
    for (const wchar_t* candidate : candidates) {
        wchar_t path[MAX_PATH]{};
        wcscpy_s(path, system_directory);
        if (!append_path(path, std::size(path), L"\\") ||
            !append_path(path, std::size(path), candidate)) {
            continue;
        }
        HMODULE module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (module == nullptr) continue;
        const auto get_state = reinterpret_cast<XInputGetStateFn>(
            GetProcAddress(module, "XInputGetState"));
        const auto set_state = reinterpret_cast<XInputSetStateFn>(
            GetProcAddress(module, "XInputSetState"));
        if (get_state != nullptr && set_state != nullptr) {
            g_xinput_module = module;
            g_xinput_get_state = get_state;
            g_xinput_set_state = set_state;
            log_line("loaded XInput backend %ls", candidate);
            return true;
        }
        FreeLibrary(module);
    }
    return false;
}

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
