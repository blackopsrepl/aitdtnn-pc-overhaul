// Rumble configuration, logging, executable validation and XInput loading.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

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
