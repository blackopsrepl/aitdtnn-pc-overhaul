// Win32/OpenGL test helpers and capture-file discovery.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gl/GL.h>

#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

#include "../src/renderer_api.hpp"

using GlClearFn = void(APIENTRY*)(GLbitfield);
using GlViewportFn = void(APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
using GlScissorFn = void(APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
using GlDrawBufferFn = void(APIENTRY*)(GLenum);
using GlReadBufferFn = void(APIENTRY*)(GLenum);
using GlFogfFn = void(APIENTRY*)(GLenum, GLfloat);
using GlTexParameteriFn = void(APIENTRY*)(GLenum, GLenum, GLint);
using GlTexParameterfFn = void(APIENTRY*)(GLenum, GLenum, GLfloat);
using WglCreateContextFn = HGLRC(WINAPI*)(HDC);
using WglMakeCurrentFn = BOOL(WINAPI*)(HDC, HGLRC);
using WglDeleteContextFn = BOOL(WINAPI*)(HGLRC);
using SwapBuffersFn = BOOL(WINAPI*)(HDC);
using InitializeFn = DWORD(WINAPI*)(void*);
using DiagnosticsFn = DWORD(WINAPI*)(aitd4::RendererDiagnostics*);
using CaptureFn = DWORD(WINAPI*)(DWORD);
using ChangeDisplaySettingsAFn = LONG(WINAPI*)(DEVMODEA*, DWORD);

volatile ChangeDisplaySettingsAFn required_change_display_import = ChangeDisplaySettingsA;

__declspec(noinline) FARPROC lookup(HMODULE module, const char* name) {
    return GetProcAddress(module, name);
}

bool file_time_at_or_after(const FILETIME& candidate, const FILETIME& threshold) {
    return CompareFileTime(&candidate, &threshold) >= 0;
}

std::string find_new_capture(const char* directory, const char* label, const FILETIME& threshold) {
    char pattern[MAX_PATH]{};
    std::snprintf(pattern, MAX_PATH, "%s\\aitd4-renderer-%s-*.tga", directory, label);
    WIN32_FIND_DATAA data{};
    HANDLE search = FindFirstFileA(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) return {};
    std::string selected;
    FILETIME selected_time{};
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            file_time_at_or_after(data.ftLastWriteTime, threshold) &&
            (selected.empty() || CompareFileTime(&data.ftLastWriteTime, &selected_time) > 0)) {
            char path[MAX_PATH]{};
            std::snprintf(path, MAX_PATH, "%s\\%s", directory, data.cFileName);
            selected = path;
            selected_time = data.ftLastWriteTime;
        }
    } while (FindNextFileA(search, &data));
    FindClose(search);
    return selected;
}

bool read_tga_sample(const std::string& path, int expected_width, int expected_height,
                     int x, int y, unsigned char pixel[4]) {
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") != 0 || !file) return false;
    unsigned char header[18]{};
    bool ok = std::fread(header, 1, sizeof(header), file) == sizeof(header);
    const int width = header[12] | (header[13] << 8);
    const int height = header[14] | (header[15] << 8);
    ok = ok && header[2] == 2 && header[16] == 32 && width == expected_width &&
         height == expected_height && x >= 0 && x < width && y >= 0 && y < height;
    if (ok) {
        const long offset = 18 + static_cast<long>((static_cast<long long>(y) * width + x) * 4);
        ok = std::fseek(file, offset, SEEK_SET) == 0 && std::fread(pixel, 1, 4, file) == 4;
    }
    std::fclose(file);
    return ok;
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CLOSE || message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}
