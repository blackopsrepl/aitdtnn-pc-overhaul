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

extern "C" FARPROC WINAPI ResolveVersionExport(DWORD index) noexcept {
    if (index >= std::size(g_version_exports)) {
        fail_proxy_forwarding("Invalid version.dll proxy export index.");
    }
    if (!InitOnceExecuteOnce(&g_version_once, initialize_version_forwarding,
                             nullptr, nullptr)) {
        fail_proxy_forwarding(
            "Could not load and resolve the absolute system version.dll.", GetLastError());
    }
    return g_version_exports[index];
}

#define AITD_VERSION_FORWARDER(name, index)               \
    extern "C" __declspec(naked) void Proxy_##name() {   \
        __asm push index                                  \
        __asm call ResolveVersionExport                   \
        __asm jmp eax                                     \
    }

AITD_VERSION_FORWARDER(GetFileVersionInfoA, 0)
AITD_VERSION_FORWARDER(GetFileVersionInfoByHandle, 1)
AITD_VERSION_FORWARDER(GetFileVersionInfoExA, 2)
AITD_VERSION_FORWARDER(GetFileVersionInfoExW, 3)
AITD_VERSION_FORWARDER(GetFileVersionInfoSizeA, 4)
AITD_VERSION_FORWARDER(GetFileVersionInfoSizeExA, 5)
AITD_VERSION_FORWARDER(GetFileVersionInfoSizeExW, 6)
AITD_VERSION_FORWARDER(GetFileVersionInfoSizeW, 7)
AITD_VERSION_FORWARDER(GetFileVersionInfoW, 8)
AITD_VERSION_FORWARDER(VerFindFileA, 9)
AITD_VERSION_FORWARDER(VerFindFileW, 10)
AITD_VERSION_FORWARDER(VerInstallFileA, 11)
AITD_VERSION_FORWARDER(VerInstallFileW, 12)
AITD_VERSION_FORWARDER(VerLanguageNameA, 13)
AITD_VERSION_FORWARDER(VerLanguageNameW, 14)
AITD_VERSION_FORWARDER(VerQueryValueA, 15)
AITD_VERSION_FORWARDER(VerQueryValueW, 16)

#undef AITD_VERSION_FORWARDER

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(module);

    const char* failure = nullptr;
    if (!install_entrypoint_gate(&failure)) {
        append_log(failure);
        OutputDebugStringA(failure);
        OutputDebugStringA("\r\n");
        return FALSE;
    }
    return TRUE;
}
