#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string quote_argument(const char* value) {
    std::string result{"\""};
    for (const char* cursor = value; *cursor; ++cursor) {
        if (*cursor == '\"') result += '\\';
        result += *cursor;
    }
    result += '\"';
    return result;
}

bool absolute_existing_file(const char* input, char (&output)[MAX_PATH]) {
    const DWORD length = GetFullPathNameA(input, MAX_PATH, output, nullptr);
    return length > 0 && length < MAX_PATH &&
           GetFileAttributesA(output) != INVALID_FILE_ATTRIBUTES;
}

bool wait_for_thread(HANDLE thread, DWORD& result) {
    if (WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0) return false;
    return GetExitCodeThread(thread, &result) != FALSE;
}

std::uintptr_t inject_library(HANDLE process, const char* dll_path) {
    const SIZE_T path_bytes = std::strlen(dll_path) + 1;
    void* remote_path = VirtualAllocEx(
        process, nullptr, path_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) return 0;
    bool ok = WriteProcessMemory(process, remote_path, dll_path, path_bytes, nullptr) != FALSE;
    HANDLE thread = nullptr;
    if (ok) {
        const HMODULE kernel = GetModuleHandleA("kernel32.dll");
        const auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(kernel, "LoadLibraryA"));
        thread = CreateRemoteThread(process, nullptr, 0, load_library, remote_path, 0, nullptr);
        ok = thread != nullptr;
    }
    DWORD module_base = 0;
    if (ok) ok = wait_for_thread(thread, module_base) && module_base != 0;
    if (thread) CloseHandle(thread);
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    return ok ? static_cast<std::uintptr_t>(module_base) : 0;
}

bool call_remote_export(HANDLE process, const char* dll_path, std::uintptr_t remote_module,
                        const char* export_name) {
    HMODULE local_module = LoadLibraryExA(dll_path, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!local_module) return false;
    const FARPROC local_export = GetProcAddress(local_module, export_name);
    const std::uintptr_t export_offset = local_export
        ? reinterpret_cast<std::uintptr_t>(local_export) -
              reinterpret_cast<std::uintptr_t>(local_module)
        : 0;
    FreeLibrary(local_module);
    if (!export_offset) return false;
    const auto remote_export = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        remote_module + export_offset);
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, remote_export, nullptr, 0, nullptr);
    if (!thread) return false;
    DWORD result = 0;
    const bool ok = wait_for_thread(thread, result) && result != 0;
    CloseHandle(thread);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "Usage: aitd4-dev-stack-injector.exe <game.exe> <audio.dll> "
                     "<renderer.dll> [-- game arguments...]\n");
        return 2;
    }
    char game_path[MAX_PATH]{};
    char audio_path[MAX_PATH]{};
    char renderer_path[MAX_PATH]{};
    if (!absolute_existing_file(argv[1], game_path) ||
        !absolute_existing_file(argv[2], audio_path) ||
        !absolute_existing_file(argv[3], renderer_path)) {
        std::fprintf(stderr, "Game, audio hook, or renderer hook was not found.\n");
        return 3;
    }

    std::string command = quote_argument(game_path);
    int game_argument = argc > 4 && std::strcmp(argv[4], "--") == 0 ? 5 : 4;
    for (int index = game_argument; index < argc; ++index) {
        command += ' ';
        command += quote_argument(argv[index]);
    }
    char working_directory[MAX_PATH]{};
    strcpy_s(working_directory, game_path);
    char* slash = std::strrchr(working_directory, '\\');
    if (!slash) return 4;
    *slash = '\0';

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(game_path, command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                        nullptr, working_directory, &startup, &process)) {
        std::fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError());
        return 5;
    }

    int failure = 0;
    const std::uintptr_t audio_module = inject_library(process.hProcess, audio_path);
    if (!audio_module) {
        std::fprintf(stderr, "Audio hook injection failed: %lu\n", GetLastError());
        failure = 6;
    }
    std::uintptr_t renderer_module = 0;
    if (!failure) {
        renderer_module = inject_library(process.hProcess, renderer_path);
        if (!renderer_module) {
            std::fprintf(stderr, "Renderer hook injection failed: %lu\n", GetLastError());
            failure = 7;
        }
    }
    if (!failure && !call_remote_export(process.hProcess, renderer_path, renderer_module,
                                        "AITD4_Initialize")) {
        std::fprintf(stderr, "Renderer initialization failed.\n");
        failure = 8;
    }
    if (failure) {
        TerminateProcess(process.hProcess, static_cast<UINT>(failure));
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return failure;
    }

    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
}
