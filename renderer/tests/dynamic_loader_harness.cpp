#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../src/renderer_api.hpp"

using InitializeFn = DWORD(WINAPI*)(void*);
using DiagnosticsFn = DWORD(WINAPI*)(aitd4::RendererDiagnostics*);
using TestNativeMovieFrameFn = DWORD(WINAPI*)(void*, void*);
using ChoosePixelFormatFn = int(WINAPI*)(HDC, const PIXELFORMATDESCRIPTOR*);
using SetPixelFormatFn = BOOL(WINAPI*)(HDC, int, const PIXELFORMATDESCRIPTOR*);
using SwapBuffersFn = BOOL(WINAPI*)(HDC);
using WglCreateContextFn = HGLRC(WINAPI*)(HDC);
using WglMakeCurrentFn = BOOL(WINAPI*)(HDC, HGLRC);
using WglDeleteContextFn = BOOL(WINAPI*)(HGLRC);
using GlClearColorFn = void(APIENTRY*)(float, float, float, float);
using GlClearFn = void(APIENTRY*)(unsigned int);
using GlViewportFn = void(APIENTRY*)(int, int, int, int);
using GlGetIntegervFn = void(APIENTRY*)(unsigned int, int*);
using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using ChangeDisplaySettingsAFn = LONG(WINAPI*)(DEVMODEA*, DWORD);

volatile ChangeDisplaySettingsAFn required_change_display_import = ChangeDisplaySettingsA;

constexpr unsigned int gl_color_buffer_bit = 0x00004000;
constexpr unsigned int gl_viewport = 0x0BA2;
constexpr unsigned int gl_scissor_box = 0x0C10;

struct BinkStubState {
    std::uint32_t movie_open_calls{};
    std::uint32_t movie_open_flags{};
    std::uint32_t open_width{};
    std::uint32_t open_height{};
    std::uint32_t scale_width{};
    std::uint32_t scale_height{};
    int offset_x{};
    int offset_y{};
    std::uint32_t do_frame_calls{};
    std::uint32_t copy_calls{};
    std::uint32_t blit_calls{};
    std::uint32_t lock_calls{};
    std::uint32_t unlock_calls{};
    std::uint32_t get_rects_calls{};
    std::uint32_t next_frame_calls{};
    std::uint32_t close_calls{};
};

extern "C" __declspec(dllimport) void* WINAPI BinkOpen(const char*, std::uint32_t);
extern "C" __declspec(dllimport) std::uint32_t WINAPI BinkDoFrame(void*);
extern "C" __declspec(dllimport) std::uint32_t WINAPI BinkCopyToBuffer(
    void*, void*, std::int32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
extern "C" __declspec(dllimport) void WINAPI BinkNextFrame(void*);
extern "C" __declspec(dllimport) void WINAPI BinkClose(void*);
extern "C" __declspec(dllimport) void* WINAPI BinkBufferOpen(
    HWND, std::uint32_t, std::uint32_t, std::uint32_t);
extern "C" __declspec(dllimport) int WINAPI BinkBufferSetOffset(void*, int, int);
extern "C" __declspec(dllimport) void WINAPI BinkBufferBlit(void*, void*, std::uint32_t);
extern "C" __declspec(dllimport) int WINAPI BinkBufferLock(void*);
extern "C" __declspec(dllimport) void WINAPI BinkBufferUnlock(void*);
extern "C" __declspec(dllimport) void WINAPI BinkStubGetState(BinkStubState*);

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CLOSE || message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

template <typename T>
T lookup(HMODULE module, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

GetProcAddressFn current_iat_get_proc_address() {
    auto base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    auto descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        const char* module_name = reinterpret_cast<const char*>(base + descriptor->Name);
        if (_stricmp(module_name, "KERNEL32.dll") != 0) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + (descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk
                                                   : descriptor->FirstThunk));
        auto functions = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++functions) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), "GetProcAddress") == 0)
                return reinterpret_cast<GetProcAddressFn>(functions->u1.Function);
        }
    }
    return nullptr;
}

template <typename T>
T dynamic_lookup(GetProcAddressFn get_proc_address, HMODULE module, const char* name) {
    return reinterpret_cast<T>(get_proc_address(module, name));
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    if (!required_change_display_import) return 13;
    char executable[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, executable, MAX_PATH)) return 1;
    char* filename = std::strrchr(executable, '\\');
    if (!filename) return 2;
    strcpy_s(filename + 1, MAX_PATH - static_cast<std::size_t>(filename + 1 - executable),
             "aitd4-renderer-hook-test.dll");
    HMODULE renderer = LoadLibraryA(executable);
    if (!renderer) return 3;
    const auto initialize = lookup<InitializeFn>(renderer, "AITD4_Initialize");
    const auto diagnostics = lookup<DiagnosticsFn>(renderer, "AITD4_GetRendererDiagnostics");
    const auto test_native_movie_frame = lookup<TestNativeMovieFrameFn>(
        renderer, "_AITD4_TestNativeMovieFrame@8");
    if (!initialize || !diagnostics || !initialize(nullptr) || !initialize(nullptr)) return 4;
    if (GetSystemMetrics(SM_CXSCREEN) * 3 != GetSystemMetrics(SM_CYSCREEN) * 4) return 18;
    const auto get_proc_address = current_iat_get_proc_address();
    if (!get_proc_address) return 12;

    WNDCLASSA cls{};
    cls.style = CS_OWNDC;
    cls.lpfnWndProc = window_proc;
    cls.hInstance = instance;
    cls.lpszClassName = "AITD4 Dynamic GL Harness";
    if (!RegisterClassA(&cls)) return 5;
    HWND window = CreateWindowExA(0, cls.lpszClassName, "AITD4 Dynamic GL Harness",
                                  WS_OVERLAPPEDWINDOW, 0, 0, 1280, 960,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) return 6;
    SetWindowPos(window, nullptr, 0, 0, 1280, 960, SWP_NOZORDER | SWP_NOACTIVATE);

    aitd4::RendererDiagnostics geometry{};
    if (!diagnostics(&geometry) || !geometry.initialized || !geometry.bink_hooks_ready) return 16;
    char custom_logical[8]{};
    const bool require_decoupled = GetEnvironmentVariableA(
        "AITD4_TEST_LOGICAL_WIDTH", custom_logical, sizeof(custom_logical)) != 0;
    if (require_decoupled && geometry.render_width == geometry.viewport_width) return 17;
    const std::uint32_t expected_movie_width = geometry.viewport_width;
    const std::uint32_t expected_movie_height = static_cast<std::uint32_t>(
        MulDiv(320, static_cast<int>(geometry.viewport_height), 480));
    const int expected_movie_x = geometry.viewport_x;
    const int expected_movie_y = geometry.viewport_y +
        MulDiv(80, static_cast<int>(geometry.viewport_height), 480);

    HMODULE gdi = LoadLibraryA("gdi32.dll");
    HMODULE gl = LoadLibraryA("opengl32.dll");
    const auto choose_pixel_format = dynamic_lookup<ChoosePixelFormatFn>(
        get_proc_address, gdi, "ChoosePixelFormat");
    const auto set_pixel_format = dynamic_lookup<SetPixelFormatFn>(
        get_proc_address, gdi, "SetPixelFormat");
    const auto swap_buffers = dynamic_lookup<SwapBuffersFn>(
        get_proc_address, gdi, "SwapBuffers");
    const auto create_context = dynamic_lookup<WglCreateContextFn>(
        get_proc_address, gl, "wglCreateContext");
    const auto make_current = dynamic_lookup<WglMakeCurrentFn>(
        get_proc_address, gl, "wglMakeCurrent");
    const auto delete_context = dynamic_lookup<WglDeleteContextFn>(
        get_proc_address, gl, "wglDeleteContext");
    const auto clear_color = dynamic_lookup<GlClearColorFn>(
        get_proc_address, gl, "glClearColor");
    const auto clear = dynamic_lookup<GlClearFn>(get_proc_address, gl, "glClear");
    const auto viewport = dynamic_lookup<GlViewportFn>(get_proc_address, gl, "glViewport");
    const auto get_integer = dynamic_lookup<GlGetIntegervFn>(
        get_proc_address, gl, "glGetIntegerv");
    if (!choose_pixel_format || !set_pixel_format || !swap_buffers || !create_context ||
        !make_current || !delete_context || !clear_color || !clear || !viewport || !get_integer)
        return 7;

    HDC dc = GetDC(window);
    PIXELFORMATDESCRIPTOR format{};
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    format.cDepthBits = 24;
    format.cStencilBits = 8;
    const int index = choose_pixel_format(dc, &format);
    if (!index || !set_pixel_format(dc, index, &format)) return 8;
    HGLRC context = create_context(dc);
    if (!context || !make_current(dc, context)) return 9;
    aitd4::RendererDiagnostics initialized{};
    int initial_viewport[4]{};
    int initial_scissor[4]{};
    get_integer(gl_viewport, initial_viewport);
    get_integer(gl_scissor_box, initial_scissor);
    if (!diagnostics(&initialized) || !initialized.framebuffer_ready ||
        initial_viewport[0] != 0 || initial_viewport[1] != 0 ||
        initial_viewport[2] != static_cast<int>(initialized.render_width) ||
        initial_viewport[3] != static_cast<int>(initialized.render_height) ||
        initial_scissor[0] != 0 || initial_scissor[1] != 0 ||
        initial_scissor[2] != static_cast<int>(initialized.render_width) ||
        initial_scissor[3] != static_cast<int>(initialized.render_height)) return 21;

    constexpr std::uint32_t movie_flags = 0x00200000;
    void* movie = BinkOpen("stub-movie.bik", movie_flags);
    const bool require_movie_fallback = GetEnvironmentVariableA(
        "AITD4_TEST_BINK_FIRST_OPEN_REJECT", nullptr, 0) != 0;
    if (require_movie_fallback) {
        if (movie) return 20;
        movie = BinkOpen("alternate\\stub-movie.bik", movie_flags);
    }
    if (!movie) return 19;
    const bool crt_enabled = initialized.crt_enabled != 0;
    const bool alternate_gl_bink = crt_enabled && GetEnvironmentVariableA(
        "AITD4_TEST_BINK_ALTERNATE_GL", nullptr, 0) != 0;
    void* bink_buffer = nullptr;
    if (!alternate_gl_bink) {
        bink_buffer = BinkBufferOpen(window, 640, 320, 0);
        if (!bink_buffer || !BinkBufferSetOffset(bink_buffer, 0, 80)) return 14;
    }
    std::vector<std::uint8_t> movie_pixels(static_cast<std::size_t>(640) * 320 * 3);
    for (std::size_t pixel_index = 0; pixel_index < movie_pixels.size(); pixel_index += 3) {
        movie_pixels[pixel_index + 0] =
            static_cast<std::uint8_t>((pixel_index / 3) & 0xff);
        movie_pixels[pixel_index + 1] = 96;
        movie_pixels[pixel_index + 2] = 160;
    }
    if (alternate_gl_bink) {
        if (BinkDoFrame(movie) != 1 ||
            BinkCopyToBuffer(movie, movie_pixels.data(), 640 * 3, 320, 0, 0, 2) != 1 ||
            !swap_buffers(dc)) return 24;
        BinkNextFrame(movie);
    } else if (crt_enabled) {
        if (!test_native_movie_frame || !test_native_movie_frame(movie, bink_buffer)) return 23;
    } else {
        if (!BinkBufferLock(bink_buffer) || BinkDoFrame(movie) != 1 ||
            BinkCopyToBuffer(movie, movie_pixels.data(), 640 * 3, 320, 0, 0, 2) != 1)
            return 22;
        BinkBufferUnlock(bink_buffer);
        BinkBufferBlit(bink_buffer, nullptr, 0);
        BinkNextFrame(movie);
    }
    BinkClose(movie);

    BinkStubState bink_state{};
    BinkStubGetState(&bink_state);
    const std::uint32_t expected_movie_open_calls = require_movie_fallback ? 2u : 1u;
    if (bink_state.movie_open_calls != expected_movie_open_calls ||
        bink_state.movie_open_flags != movie_flags ||
        bink_state.open_width != 640 || bink_state.open_height != 320 ||
        bink_state.do_frame_calls != 1 || bink_state.copy_calls != 1 ||
        bink_state.blit_calls != (crt_enabled ? 0u : 1u) ||
        bink_state.lock_calls != (alternate_gl_bink ? 0u : 1u) ||
        bink_state.unlock_calls != (alternate_gl_bink ? 0u : 1u) ||
        bink_state.get_rects_calls != (crt_enabled && !alternate_gl_bink ? 1u : 0u) ||
        bink_state.next_frame_calls != 1 || bink_state.close_calls != 1 ||
        bink_state.scale_width != (crt_enabled ? 0u : expected_movie_width) ||
        bink_state.scale_height != (crt_enabled ? 0u : expected_movie_height) ||
        bink_state.offset_x != (crt_enabled ? 0 : expected_movie_x) ||
        bink_state.offset_y != (alternate_gl_bink ? 0 : (crt_enabled ? 80 : expected_movie_y)))
        return 15;
    viewport(0, 0, 1280, 960);
    clear_color(0.02f, 0.04f, 0.06f, 1.0f);
    clear(gl_color_buffer_bit);
    if (!swap_buffers(dc)) return 10;

    aitd4::RendererDiagnostics status{};
    if (!diagnostics(&status) || !status.initialized || !status.framebuffer_ready ||
        !status.bink_hooks_ready || status.bink_source_width != 640 ||
        status.bink_source_height != 320 || status.bink_output_width != expected_movie_width ||
        status.bink_output_height != expected_movie_height ||
        status.bink_output_x != expected_movie_x || status.bink_output_y != expected_movie_y ||
        status.crt_ready != (crt_enabled ? 1u : 0u) ||
        status.bink_presented_frames != (crt_enabled ? 1u : 0u) ||
        status.bink_native_blits_suppressed != (crt_enabled && !alternate_gl_bink ? 1u : 0u) ||
        status.bink_last_surface_format != (crt_enabled ? 2u : 0u) ||
        status.bink_last_pitch != (crt_enabled ? 640 * 3 : 0) ||
        status.gl_major < 3 || (status.gl_major == 3 && status.gl_minor < 3) ||
        status.render_width * 3 != status.render_height * 4) return 11;
    std::printf("dynamic loader harness passed gl=%lu.%lu logical=%lux%lu output=%lux%lu\n",
                status.gl_major, status.gl_minor, status.render_width, status.render_height,
                status.viewport_width, status.viewport_height);
    make_current(nullptr, nullptr);
    delete_context(context);
    ReleaseDC(window, dc);
    DestroyWindow(window);
    return 0;
}
