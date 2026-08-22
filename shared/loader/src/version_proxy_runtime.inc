// System Version API forwarding state, validation, logging and path helpers.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>

#include "sha256.hpp"

namespace {

constexpr unsigned char kSupportedEntrypointBytes[5] = {0x55, 0x8b, 0xec, 0x6a, 0xff};
constexpr unsigned char kSupported15SlotExeSha256[32] = {
    0x56, 0x68, 0x11, 0x8e, 0x0e, 0x19, 0xd5, 0x69,
    0x98, 0x65, 0x00, 0xa1, 0xc8, 0x05, 0xa8, 0x53,
    0x97, 0xc8, 0x68, 0x1e, 0x7b, 0x67, 0x2b, 0x49,
    0xa6, 0x86, 0x45, 0x46, 0x2e, 0xcc, 0xc6, 0x72,
};
constexpr unsigned char kSupportedRetailExeSha256[32] = {
    0x32, 0x09, 0x08, 0xaf, 0x4c, 0xe5, 0xc7, 0x24,
    0xb6, 0x0a, 0x7e, 0xea, 0x6a, 0x5a, 0xad, 0xe7,
    0x37, 0xd5, 0x1d, 0x65, 0xae, 0xe8, 0x50, 0x67,
    0x44, 0xfc, 0xe6, 0xe6, 0xdd, 0x01, 0x43, 0xe0,
};
constexpr wchar_t kLogName[] = L"aitdtnn-overhaul-loader.log";
constexpr wchar_t kAudioRelativePath[] =
    L"audio-restoration\\aitd4-audio-hook.dll";
constexpr wchar_t kRendererRelativePath[] =
    L"renderer\\aitd4-renderer-hook.dll";
constexpr wchar_t kRumbleRelativePath[] =
    L"rumble\\aitd4-rumble-hook.dll";

constexpr const char* kVersionExportNames[17] = {
    "GetFileVersionInfoA",
    "GetFileVersionInfoByHandle",
    "GetFileVersionInfoExA",
    "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA",
    "GetFileVersionInfoSizeExA",
    "GetFileVersionInfoSizeExW",
    "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW",
    "VerFindFileA",
    "VerFindFileW",
    "VerInstallFileA",
    "VerInstallFileW",
    "VerLanguageNameA",
    "VerLanguageNameW",
    "VerQueryValueA",
    "VerQueryValueW",
};

DWORD g_entrypoint_continue = 0;
HMODULE g_real_version = nullptr;
FARPROC g_version_exports[17]{};
INIT_ONCE g_version_once = INIT_ONCE_STATIC_INIT;
HMODULE g_audio_module = nullptr;
HMODULE g_renderer_module = nullptr;
HMODULE g_rumble_module = nullptr;

bool executable_path(wchar_t* path, DWORD capacity) noexcept {
    if (capacity == 0) return false;
    const DWORD length = GetModuleFileNameW(nullptr, path, capacity);
    return length != 0 && length < capacity;
}

bool executable_directory(wchar_t* path, DWORD capacity) noexcept {
    if (!executable_path(path, capacity)) return false;
    wchar_t* slash = std::wcsrchr(path, L'\\');
    if (slash == nullptr) return false;
    slash[1] = L'\0';
    return true;
}

void append_log(const char* message) noexcept {
    wchar_t path[32768]{};
    if (!executable_directory(path, static_cast<DWORD>(std::size(path)))) return;
    const std::size_t length = std::wcslen(path);
    if (length + std::size(kLogName) > std::size(path)) return;
    wcscpy_s(path + length, std::size(path) - length, kLogName);

    HANDLE file = CreateFileW(path, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    const DWORD message_length = static_cast<DWORD>(std::strlen(message));
    WriteFile(file, message, message_length, &written, nullptr);
    constexpr char newline[] = "\r\n";
    WriteFile(file, newline, 2, &written, nullptr);
    CloseHandle(file);
}

__declspec(noreturn) void fail_closed(const char* message, DWORD error = 0) noexcept {
    char full_message[1024]{};
    if (error != 0) {
        sprintf_s(full_message, "%s (Win32 error %lu)", message,
                  static_cast<unsigned long>(error));
    } else {
        strcpy_s(full_message, message);
    }
    append_log(full_message);
    OutputDebugStringA(full_message);
    OutputDebugStringA("\r\n");
    MessageBoxA(nullptr, full_message, "AITD:TNN overhaul loader",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    TerminateProcess(GetCurrentProcess(), ERROR_DLL_INIT_FAILED);
    ExitProcess(ERROR_DLL_INIT_FAILED);
}

__declspec(noreturn) void fail_proxy_forwarding(const char* message,
                                                DWORD error = 0) noexcept {
    char full_message[1024]{};
    if (error != 0) {
        sprintf_s(full_message, "%s (Win32 error %lu)", message,
                  static_cast<unsigned long>(error));
    } else {
        strcpy_s(full_message, message);
    }
    append_log(full_message);
    OutputDebugStringA(full_message);
    OutputDebugStringA("\r\n");
    TerminateProcess(GetCurrentProcess(), ERROR_DLL_INIT_FAILED);
    ExitProcess(ERROR_DLL_INIT_FAILED);
}

BOOL CALLBACK initialize_version_forwarding(PINIT_ONCE, PVOID, PVOID*) noexcept {
    wchar_t system_path[MAX_PATH]{};
    UINT length = GetSystemWow64DirectoryW(system_path,
                                            static_cast<UINT>(std::size(system_path)));
    if (length == 0 && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED) {
        length = GetSystemDirectoryW(system_path,
                                     static_cast<UINT>(std::size(system_path)));
    }
    constexpr wchar_t suffix[] = L"\\version.dll";
    if (length == 0 || length + std::size(suffix) > std::size(system_path)) return FALSE;
    wcscpy_s(system_path + length, std::size(system_path) - length, suffix);

    g_real_version = LoadLibraryExW(system_path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (g_real_version == nullptr) return FALSE;
    for (std::size_t index = 0; index < std::size(g_version_exports); ++index) {
        g_version_exports[index] = GetProcAddress(g_real_version, kVersionExportNames[index]);
        if (g_version_exports[index] == nullptr) return FALSE;
    }
    return TRUE;
}

bool hash_file(const wchar_t* path, unsigned char digest[32], DWORD* error) noexcept {
    HANDLE file = CreateFileW(path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return false;
    }

    aitdtnn::loader::Sha256 sha;
    unsigned char buffer[64 * 1024]{};
    bool ok = true;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) {
            *error = GetLastError();
            ok = false;
            break;
        }
        if (read == 0) break;
        sha.update(buffer, read);
    }
    CloseHandle(file);
    if (!ok) return false;
    sha.finish(digest);
    *error = ERROR_SUCCESS;
    return true;
}

void digest_to_hex(const unsigned char digest[32], char output[65]) noexcept {
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t index = 0; index < 32; ++index) {
        output[index * 2] = digits[digest[index] >> 4u];
        output[index * 2 + 1] = digits[digest[index] & 0x0fu];
    }
    output[64] = '\0';
}

bool module_path(const wchar_t* relative, wchar_t* output, std::size_t capacity) noexcept {
    if (!executable_directory(output, static_cast<DWORD>(capacity))) return false;
    const std::size_t prefix_length = std::wcslen(output);
    const std::size_t relative_length = std::wcslen(relative);
    if (prefix_length + relative_length + 1 > capacity) return false;
    wcscpy_s(output + prefix_length, capacity - prefix_length, relative);
    return true;
}
