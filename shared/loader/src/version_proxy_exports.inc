// Lazy Version export resolution, forwarder stubs and DLL entry point.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

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
