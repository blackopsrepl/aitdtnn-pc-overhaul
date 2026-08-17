#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "graphics_hook.hpp"
#include "runtime.hpp"

namespace {
HMODULE g_self{};
SRWLOCK g_initialization_lock = SRWLOCK_INIT;
CONDITION_VARIABLE g_initialization_changed = CONDITION_VARIABLE_INIT;
LONG g_initialization_state{};
}

extern "C" DWORD WINAPI AITD4_Initialize(void*) {
    AcquireSRWLockExclusive(&g_initialization_lock);
    while (g_initialization_state == 1) {
        SleepConditionVariableSRW(&g_initialization_changed, &g_initialization_lock,
                                  INFINITE, 0);
    }
    if (g_initialization_state == 2 || g_initialization_state == 3) {
        const DWORD result = g_initialization_state == 2 ? 1u : 0u;
        ReleaseSRWLockExclusive(&g_initialization_lock);
        return result;
    }
    g_initialization_state = 1;
    ReleaseSRWLockExclusive(&g_initialization_lock);

    bool ok = aitd4::initialize_runtime(g_self) &&
              aitd4::validate_supported_executable();
    if (ok && !aitd4::install_graphics_hooks()) {
        aitd4::log_line("graphics hook initialization failed");
        ok = false;
    }
    if (ok) aitd4::log_line("renderer hook ready");

    AcquireSRWLockExclusive(&g_initialization_lock);
    g_initialization_state = ok ? 2 : 3;
    WakeAllConditionVariable(&g_initialization_changed);
    ReleaseSRWLockExclusive(&g_initialization_lock);
    return ok ? 1u : 0u;
}

extern "C" DWORD WINAPI AITD4_GetRendererDiagnostics(
    aitd4::RendererDiagnostics* diagnostics) {
    return aitd4::get_renderer_diagnostics(diagnostics) ? 1 : 0;
}

extern "C" DWORD WINAPI AITD4_RequestRendererCapture(DWORD flags) {
    return aitd4::request_renderer_capture(flags) ? 1 : 0;
}

BOOL WINAPI DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = self;
        DisableThreadLibraryCalls(self);
    }
    return TRUE;
}
