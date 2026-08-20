#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>

#include "bink_frame.hpp"
#include "character_select_movie_gate.hpp"
#include "cutscene_input_guard.hpp"
#include "graphics_hook.hpp"
#include "movie_skip_gate.hpp"
#include "runtime.hpp"
#include "viewport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")

namespace aitd4 {
namespace {

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_RGBA8 0x8058
#define GL_RGBA16F 0x881A
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_TEXTURE0 0x84C0
#define GL_MAX_SAMPLES 0x8D57
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#endif

using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using CreateWindowExAFn = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int,
                                        HWND, HMENU, HINSTANCE, LPVOID);
using SetWindowPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using GetClientRectFn = BOOL(WINAPI*)(HWND, LPRECT);
using GetSystemMetricsFn = int(WINAPI*)(int);
using ChangeDisplaySettingsAFn = LONG(WINAPI*)(DEVMODEA*, DWORD);
using SwapBuffersFn = BOOL(WINAPI*)(HDC);
using ChoosePixelFormatFn = int(WINAPI*)(HDC, const PIXELFORMATDESCRIPTOR*);
using WglCreateContextFn = HGLRC(WINAPI*)(HDC);
using WglCreateContextAttribsFn = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using WglDeleteContextFn = BOOL(WINAPI*)(HGLRC);
using WglMakeCurrentFn = BOOL(WINAPI*)(HDC, HGLRC);
using GlClearFn = void(APIENTRY*)(GLbitfield);
using GlViewportFn = void(APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
using GlScissorFn = void(APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
using GlTexParameteriFn = void(APIENTRY*)(GLenum, GLenum, GLint);
using GlTexParameterfFn = void(APIENTRY*)(GLenum, GLenum, GLfloat);
using GlTexParameterfvFn = void(APIENTRY*)(GLenum, GLenum, const GLfloat*);
using GlTexParameterivFn = void(APIENTRY*)(GLenum, GLenum, const GLint*);
using GlDrawBufferFn = void(APIENTRY*)(GLenum);
using GlReadBufferFn = void(APIENTRY*)(GLenum);
using GlPixelZoomFn = void(APIENTRY*)(GLfloat, GLfloat);
using GlDrawPixelsFn = void(APIENTRY*)(GLsizei, GLsizei, GLenum, GLenum, const void*);
using GlTexImage2DFn = void(APIENTRY*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,
                                      GLenum, const void*);
using GlTexSubImage2DFn = void(APIENTRY*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum,
                                         GLenum, const void*);

using GenFramebuffersFn = void(APIENTRY*)(GLsizei, GLuint*);
using DeleteFramebuffersFn = void(APIENTRY*)(GLsizei, const GLuint*);
using BindFramebufferFn = void(APIENTRY*)(GLenum, GLuint);
using CheckFramebufferStatusFn = GLenum(APIENTRY*)(GLenum);
using FramebufferTexture2DFn = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using BlitFramebufferFn = void(APIENTRY*)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint,
                                          GLbitfield, GLenum);
using GenRenderbuffersFn = void(APIENTRY*)(GLsizei, GLuint*);
using DeleteRenderbuffersFn = void(APIENTRY*)(GLsizei, const GLuint*);
using BindRenderbufferFn = void(APIENTRY*)(GLenum, GLuint);
using RenderbufferStorageMultisampleFn = void(APIENTRY*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
using FramebufferRenderbufferFn = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint);
using CreateShaderFn = GLuint(APIENTRY*)(GLenum);
using ShaderSourceFn = void(APIENTRY*)(GLuint, GLsizei, const char* const*, const GLint*);
using CompileShaderFn = void(APIENTRY*)(GLuint);
using GetShaderivFn = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GetShaderInfoLogFn = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
using CreateProgramFn = GLuint(APIENTRY*)();
using AttachShaderFn = void(APIENTRY*)(GLuint, GLuint);
using LinkProgramFn = void(APIENTRY*)(GLuint);
using GetProgramivFn = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GetProgramInfoLogFn = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
using DeleteShaderFn = void(APIENTRY*)(GLuint);
using DeleteProgramFn = void(APIENTRY*)(GLuint);
using UseProgramFn = void(APIENTRY*)(GLuint);
using GetUniformLocationFn = GLint(APIENTRY*)(GLuint, const char*);
using Uniform1iFn = void(APIENTRY*)(GLint, GLint);
using Uniform1fFn = void(APIENTRY*)(GLint, GLfloat);
using Uniform2fFn = void(APIENTRY*)(GLint, GLfloat, GLfloat);
using ActiveTextureFn = void(APIENTRY*)(GLenum);
using SwapIntervalFn = BOOL(WINAPI*)(int);
using BinkBufferOpenFn = void*(WINAPI*)(HWND, std::uint32_t, std::uint32_t, std::uint32_t);
using BinkBufferSetOffsetFn = int(WINAPI*)(void*, int, int);
using BinkBufferSetScaleFn = int(WINAPI*)(void*, std::uint32_t, std::uint32_t);
using BinkOpenFn = void*(WINAPI*)(const char*, std::uint32_t);
using BinkGetErrorFn = const char*(WINAPI*)();
using BinkDoFrameFn = std::uint32_t(WINAPI*)(void*);
using BinkCopyToBufferFn = std::uint32_t(WINAPI*)(void*, void*, std::int32_t, std::uint32_t,
                                                  std::uint32_t, std::uint32_t, std::uint32_t);
using BinkBufferBlitFn = void(WINAPI*)(void*, void*, std::uint32_t);
using BinkBufferLockFn = int(WINAPI*)(void*);
using BinkBufferUnlockFn = void(WINAPI*)(void*);
using BinkNextFrameFn = void(WINAPI*)(void*);
using BinkCloseFn = void(WINAPI*)(void*);
using BinkGetRectsFn = std::uint32_t(WINAPI*)(void*, std::uint32_t);
using NativeMovieFrameFn = void(__cdecl*)(void*, void*);
using CharacterSelectAdvanceFn = void(__cdecl*)();
using PostSelectionPlatformFn = int(__cdecl*)();
using CurrentMenuItemFn = std::uint32_t(__thiscall*)(void*);
using MovieRequestFn = void(__cdecl*)(int, std::uint32_t);

GetProcAddressFn real_get_proc_address{};
CreateWindowExAFn real_create_window_ex_a{};
SetWindowPosFn real_set_window_pos{};
GetClientRectFn real_get_client_rect{};
GetSystemMetricsFn real_get_system_metrics{};
ChangeDisplaySettingsAFn real_change_display_settings_a{};
SwapBuffersFn real_swap_buffers{};
ChoosePixelFormatFn real_choose_pixel_format{};
WglCreateContextFn real_wgl_create_context{};
WglDeleteContextFn real_wgl_delete_context{};
WglMakeCurrentFn real_wgl_make_current{};
GlClearFn real_gl_clear{};
GlViewportFn real_gl_viewport{};
GlScissorFn real_gl_scissor{};
GlTexParameteriFn real_gl_tex_parameter_i{};
GlTexParameterfFn real_gl_tex_parameter_f{};
GlTexParameterfvFn real_gl_tex_parameter_fv{};
GlTexParameterivFn real_gl_tex_parameter_iv{};
GlDrawBufferFn real_gl_draw_buffer{};
GlReadBufferFn real_gl_read_buffer{};
GlPixelZoomFn real_gl_pixel_zoom{};
GlDrawPixelsFn real_gl_draw_pixels{};
GlTexImage2DFn real_gl_tex_image_2d{};
GlTexSubImage2DFn real_gl_tex_sub_image_2d{};

GenFramebuffersFn gen_framebuffers{};
DeleteFramebuffersFn delete_framebuffers{};
BindFramebufferFn bind_framebuffer{};
CheckFramebufferStatusFn check_framebuffer_status{};
FramebufferTexture2DFn framebuffer_texture_2d{};
BlitFramebufferFn blit_framebuffer{};
GenRenderbuffersFn gen_renderbuffers{};
DeleteRenderbuffersFn delete_renderbuffers{};
BindRenderbufferFn bind_renderbuffer{};
RenderbufferStorageMultisampleFn renderbuffer_storage_multisample{};
FramebufferRenderbufferFn framebuffer_renderbuffer{};
CreateShaderFn create_shader{};
ShaderSourceFn shader_source{};
CompileShaderFn compile_shader{};
GetShaderivFn get_shader_iv{};
GetShaderInfoLogFn get_shader_info_log{};
CreateProgramFn create_program{};
AttachShaderFn attach_shader{};
LinkProgramFn link_program{};
GetProgramivFn get_program_iv{};
GetProgramInfoLogFn get_program_info_log{};
DeleteShaderFn delete_shader{};
DeleteProgramFn delete_program{};
UseProgramFn use_program{};
GetUniformLocationFn get_uniform_location{};
Uniform1iFn uniform_1i{};
Uniform1fFn uniform_1f{};
Uniform2fFn uniform_2f{};
ActiveTextureFn active_texture{};
SwapIntervalFn swap_interval{};
BinkBufferOpenFn real_bink_buffer_open{};
BinkBufferSetOffsetFn real_bink_buffer_set_offset{};
BinkBufferSetScaleFn real_bink_buffer_set_scale{};
BinkOpenFn real_bink_open{};
BinkGetErrorFn real_bink_get_error{};
BinkDoFrameFn real_bink_do_frame{};
BinkCopyToBufferFn real_bink_copy_to_buffer{};
BinkBufferBlitFn real_bink_buffer_blit{};
BinkBufferLockFn real_bink_buffer_lock{};
BinkBufferUnlockFn real_bink_buffer_unlock{};
BinkNextFrameFn real_bink_next_frame{};
BinkCloseFn real_bink_close{};
BinkGetRectsFn real_bink_get_rects{};
NativeMovieFrameFn real_native_movie_frame{};
CharacterSelectAdvanceFn real_character_select_advance{};
PostSelectionPlatformFn real_post_selection_platform{};
CurrentMenuItemFn real_current_menu_item{};
MovieRequestFn real_movie_request{};

HWND game_window{};
WNDPROC game_wndproc{};
HGLRC game_context{};
RECT monitor_rect{};
Viewport output_viewport{};
int render_width{};
int render_height{};
int source_width{};
int source_height{};
bool inside_compositor{};
bool framebuffer_ready{};
bool hooks_initialized{};
GLuint multisample_fbo{};
GLuint multisample_color{};
GLuint multisample_depth_stencil{};
GLuint resolve_fbo{};
GLuint resolve_texture{};
GLuint compositor_program{};
GLint source_uniform{-1};
GLint inverse_size_uniform{-1};
GLint deband_uniform{-1};
GLint dither_uniform{-1};
GLuint crt_signal_fbo{};
GLuint crt_signal_texture{};
GLuint crt_response_fbo{};
GLuint crt_response_texture{};
GLuint crt_blur_horizontal_fbo{};
GLuint crt_blur_horizontal_texture{};
GLuint crt_blur_vertical_fbo{};
GLuint crt_blur_vertical_texture{};
GLuint bink_canvas_texture{};
GLuint crt_signal_program{};
GLuint crt_response_program{};
GLuint crt_blur_program{};
GLuint crt_present_program{};
GLint crt_signal_source_uniform{-1};
GLint crt_signal_inverse_size_uniform{-1};
GLint crt_signal_deband_uniform{-1};
GLint crt_response_source_uniform{-1};
GLint crt_response_signal_size_uniform{-1};
GLint crt_response_mask_uniform{-1};
GLint crt_response_scanline_uniform{-1};
GLint crt_blur_source_uniform{-1};
GLint crt_blur_inverse_size_uniform{-1};
GLint crt_blur_direction_uniform{-1};
GLint crt_blur_extract_uniform{-1};
GLint crt_present_source_uniform{-1};
GLint crt_present_blur_uniform{-1};
GLint crt_present_bloom_uniform{-1};
GLint crt_present_halation_uniform{-1};
GLint crt_present_dither_uniform{-1};
bool crt_ready{};
float max_anisotropy{1.0f};
int gl_major{};
int gl_minor{};
int actual_samples{1};
int actual_color_bits{};
int actual_alpha_bits{};
int actual_depth_bits{};
int actual_stencil_bits{};
bool pixel_format_logged{};
volatile LONG capture_request_flags{};
int viewport_log_count{};
int scissor_log_count{};
int startup_pixel_log_count{};
volatile LONG fmv_pixel_trace_budget{};
bool framebuffer_srgb_logged{};
bool bink_hooks_ready{};
std::uint32_t last_bink_source_width{};
std::uint32_t last_bink_source_height{};
std::uint32_t last_bink_output_width{};
void* active_bink_buffer{};
int active_bink_canvas_x{};
int active_bink_canvas_y{};
const std::uint8_t* last_bink_destination{};
std::int32_t last_bink_pitch{};
std::uint32_t last_bink_copy_height{};
std::uint32_t last_bink_copy_flags{};
std::uint32_t last_bink_copy_x{};
std::uint32_t last_bink_copy_y{};
std::vector<std::uint8_t> bink_canvas_rgba;
std::vector<std::uint8_t> bink_decode_storage;
std::uint32_t bink_presented_frames{};
std::uint32_t bink_native_blits_suppressed{};
void* active_bink_movie{};
std::uint32_t active_bink_do_frames{};
std::uint32_t active_bink_copies{};
std::uint32_t active_bink_next_frames{};
std::uint32_t active_bink_presented_frames{};
std::uint32_t active_bink_suppressed_blits{};
bool artificial_bink_lock{};
bool alternate_bink_frame_pending{};
int active_bink_request_id{-1};
std::uint32_t active_bink_request_serial{};
int current_movie_request_id{-1};
bool current_movie_request_opened{};
std::uint32_t current_movie_request_serial{};
std::uint32_t movie_request_serial{};
CutsceneInputGuard cutscene_input_guard;
MovieSkipGate movie_skip_gate;
volatile LONG post_selection_movie_in_flight{};
std::uint32_t last_bink_output_height{};
int last_bink_output_x{};
int last_bink_output_y{};

bool initialize_framebuffer();
bool present_crt_texture(GLuint input_texture, int input_width, int input_height,
                         bool apply_deband);
bool capture_frame(const char* label, GLuint framebuffer, GLenum buffer, int width, int height);
void WINAPI hooked_bink_buffer_blit(void* buffer, void* rectangles,
                                    std::uint32_t rectangle_count);
void WINAPI hooked_bink_next_frame(void* bink);
void __cdecl hooked_native_movie_frame(void* bink, void* buffer);
bool install_native_movie_frame_hook();
const char* movie_request_name(int id, char (&name)[9]);
void __cdecl hooked_movie_request(int id, std::uint32_t skip_mask);

void reset_framebuffer_state(bool destroy_objects) {
    const bool was_inside_compositor = inside_compositor;
    inside_compositor = true;
    if (destroy_objects) {
        if (compositor_program && delete_program) delete_program(compositor_program);
        if (crt_signal_program && delete_program) delete_program(crt_signal_program);
        if (crt_response_program && delete_program) delete_program(crt_response_program);
        if (crt_blur_program && delete_program) delete_program(crt_blur_program);
        if (crt_present_program && delete_program) delete_program(crt_present_program);
        const GLuint crt_textures[]{crt_signal_texture, crt_response_texture,
                                    crt_blur_horizontal_texture, crt_blur_vertical_texture,
                                    bink_canvas_texture};
        glDeleteTextures(static_cast<GLsizei>(std::size(crt_textures)), crt_textures);
        const GLuint crt_fbos[]{crt_signal_fbo, crt_response_fbo,
                                crt_blur_horizontal_fbo, crt_blur_vertical_fbo};
        if (delete_framebuffers)
            delete_framebuffers(static_cast<GLsizei>(std::size(crt_fbos)), crt_fbos);
        if (resolve_texture) glDeleteTextures(1, &resolve_texture);
        if (multisample_depth_stencil && delete_renderbuffers)
            delete_renderbuffers(1, &multisample_depth_stencil);
        if (multisample_color && delete_renderbuffers)
            delete_renderbuffers(1, &multisample_color);
        if (resolve_fbo && delete_framebuffers) delete_framebuffers(1, &resolve_fbo);
        if (multisample_fbo && delete_framebuffers) delete_framebuffers(1, &multisample_fbo);
    }
    multisample_fbo = 0;
    multisample_color = 0;
    multisample_depth_stencil = 0;
    resolve_fbo = 0;
    resolve_texture = 0;
    compositor_program = 0;
    source_uniform = -1;
    inverse_size_uniform = -1;
    deband_uniform = -1;
    dither_uniform = -1;
    crt_signal_fbo = crt_signal_texture = 0;
    crt_response_fbo = crt_response_texture = 0;
    crt_blur_horizontal_fbo = crt_blur_horizontal_texture = 0;
    crt_blur_vertical_fbo = crt_blur_vertical_texture = 0;
    bink_canvas_texture = 0;
    crt_signal_program = crt_response_program = crt_blur_program = crt_present_program = 0;
    crt_signal_source_uniform = crt_signal_inverse_size_uniform = crt_signal_deband_uniform = -1;
    crt_response_source_uniform = crt_response_signal_size_uniform = -1;
    crt_response_mask_uniform = crt_response_scanline_uniform = -1;
    crt_blur_source_uniform = crt_blur_inverse_size_uniform = crt_blur_direction_uniform = -1;
    crt_blur_extract_uniform = -1;
    crt_present_source_uniform = crt_present_blur_uniform = -1;
    crt_present_bloom_uniform = crt_present_halation_uniform = crt_present_dither_uniform = -1;
    crt_ready = false;
    framebuffer_ready = false;
    game_context = nullptr;
    inside_compositor = was_inside_compositor;
}

void scale_legacy_rectangle(GLint& x, GLint& y, GLsizei& width, GLsizei& height) {
    if (source_width <= 0 || source_height <= 0 ||
        (source_width == render_width && source_height == render_height)) return;
    const GLint right = x + width;
    const GLint top = y + height;
    const GLint scaled_x = MulDiv(x, render_width, source_width);
    const GLint scaled_y = MulDiv(y, render_height, source_height);
    const GLint scaled_right = MulDiv(right, render_width, source_width);
    const GLint scaled_top = MulDiv(top, render_height, source_height);
    x = scaled_x;
    y = scaled_y;
    width = std::max<GLsizei>(0, scaled_right - scaled_x);
    height = std::max<GLsizei>(0, scaled_top - scaled_y);
}

bool is_graphics_module(HMODULE module) {
    if (!module) return false;
    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(module, path, MAX_PATH)) return false;
    const char* filename = std::strrchr(path, '\\');
    filename = filename ? filename + 1 : path;
    return _stricmp(filename, "opengl32.dll") == 0 || _stricmp(filename, "gdi32.dll") == 0;
}

template <typename T>
bool load_gl_extension(T& output, const char* name) {
    auto wgl_get_proc = reinterpret_cast<PROC(WINAPI*)(LPCSTR)>(
        ::GetProcAddress(GetModuleHandleA("opengl32.dll"), "wglGetProcAddress"));
    output = wgl_get_proc ? reinterpret_cast<T>(wgl_get_proc(name)) : nullptr;
    const auto raw = reinterpret_cast<std::uintptr_t>(output);
    if (raw == 1 || raw == 2 || raw == 3 || raw == static_cast<std::uintptr_t>(-1))
        output = nullptr;
    if (!output) output = reinterpret_cast<T>(
        ::GetProcAddress(GetModuleHandleA("opengl32.dll"), name));
    return output != nullptr;
}

void fatal_graphics(const char* message) {
    log_line("fatal graphics error: %s", message);
#ifndef AITD4_TEST_HARNESS
    MessageBoxA(game_window, message, "AITD4 graphics overhaul",
                MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
#endif
    ExitProcess(91);
}

bool patch_iat(const char* imported_module, const char* symbol, void* replacement,
               void** original) {
    auto base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return false;
    auto descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        const char* module_name = reinterpret_cast<const char*>(base + descriptor->Name);
        if (_stricmp(module_name, imported_module) != 0) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + (descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk
                                                   : descriptor->FirstThunk));
        auto functions = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++functions) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), symbol) != 0) continue;
            DWORD old_protection = 0;
            if (!VirtualProtect(&functions->u1.Function, sizeof(functions->u1.Function),
                                PAGE_READWRITE, &old_protection)) return false;
            if (original && !*original)
                *original = reinterpret_cast<void*>(functions->u1.Function);
            functions->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            DWORD ignored = 0;
            VirtualProtect(&functions->u1.Function, sizeof(functions->u1.Function),
                           old_protection, &ignored);
            FlushInstructionCache(GetCurrentProcess(), &functions->u1.Function,
                                  sizeof(functions->u1.Function));
            log_line("IAT hook %s!%s installed", imported_module, symbol);
            return true;
        }
    }
    return false;
}

LRESULT CALLBACK hooked_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_SIZE && window == game_window) {
        lparam = MAKELPARAM(render_width, render_height);
    }
    return CallWindowProcA(game_wndproc, window, message, wparam, lparam);
}

HWND WINAPI hooked_create_window_ex_a(DWORD ex_style, LPCSTR class_name, LPCSTR window_name,
                                       DWORD style, int x, int y, int width, int height,
                                       HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter) {
    const bool candidate = !game_window && !parent && width >= 640 && height >= 480;
    const bool requested_visible = (style & WS_VISIBLE) != 0;
    if (candidate) {
        style = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        ex_style &= ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);
        x = monitor_rect.left;
        y = monitor_rect.top;
        // The original window procedure receives synchronous creation/size
        // messages before CreateWindowEx returns.  Create at the virtualized
        // 4:3 size first so the game never observes the physical 16:9 client.
        width = render_width;
        height = render_height;
    }
    HWND window = real_create_window_ex_a(ex_style, class_name, window_name, style, x, y,
                                          width, height, parent, menu, instance, parameter);
    if (candidate && window) {
        game_window = window;
        SetLastError(ERROR_SUCCESS);
        game_wndproc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hooked_wndproc)));
        if (!game_wndproc && GetLastError() != ERROR_SUCCESS) {
            log_line("window subclass failed error=%lu", GetLastError());
            game_window = nullptr;
            DestroyWindow(window);
            return nullptr;
        }
        if (!real_set_window_pos(window, HWND_TOP, monitor_rect.left, monitor_rect.top,
                                 monitor_rect.right - monitor_rect.left,
                                 monitor_rect.bottom - monitor_rect.top,
                                 SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
            log_line("physical borderless expansion failed error=%lu", GetLastError());
            DestroyWindow(window);
            game_window = nullptr;
            return nullptr;
        }
        RECT physical_client{};
        ::GetClientRect(window, &physical_client);
        if (requested_visible) ShowWindow(window, SW_SHOW);
        log_line("borderless window=%p title=%s physical=%dx%d logical=%dx%d", window,
                 window_name ? window_name : "", physical_client.right - physical_client.left,
                 physical_client.bottom - physical_client.top, render_width, render_height);
    }
    return window;
}

BOOL WINAPI hooked_set_window_pos(HWND window, HWND insert_after, int x, int y, int width,
                                  int height, UINT flags) {
    if (window == game_window) {
        x = monitor_rect.left;
        y = monitor_rect.top;
        width = monitor_rect.right - monitor_rect.left;
        height = monitor_rect.bottom - monitor_rect.top;
        flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
    }
    return real_set_window_pos(window, insert_after, x, y, width, height, flags);
}

BOOL WINAPI hooked_get_client_rect(HWND window, LPRECT rect) {
    const BOOL result = real_get_client_rect(window, rect);
    if (result && window == game_window) {
        rect->left = 0;
        rect->top = 0;
        rect->right = render_width;
        rect->bottom = render_height;
    }
    return result;
}

int WINAPI hooked_get_system_metrics(int index) {
    switch (index) {
        case SM_CXSCREEN:
        case SM_CXFULLSCREEN:
        case SM_CXVIRTUALSCREEN:
            return render_width;
        case SM_CYSCREEN:
        case SM_CYFULLSCREEN:
        case SM_CYVIRTUALSCREEN:
            return render_height;
        case SM_XVIRTUALSCREEN:
        case SM_YVIRTUALSCREEN:
            return 0;
        case SM_CMONITORS:
            return 1;
        default:
            return real_get_system_metrics(index);
    }
}

LONG WINAPI hooked_change_display_settings_a(DEVMODEA*, DWORD) {
    log_line("exclusive display-mode request suppressed");
    return DISP_CHANGE_SUCCESSFUL;
}

bool write_code_patch(void* address, const void* replacement, std::size_t size) {
    DWORD old_protection = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protection)) return false;
    std::memcpy(address, replacement, size);
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(address, size, old_protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    return restored != FALSE;
}

std::uint32_t __fastcall hooked_get_new_pressed(void* input_state, void*) {
    const auto* state = static_cast<const std::uint8_t*>(input_state);
    const std::uint32_t pressed = *reinterpret_cast<const std::uint32_t*>(state + 8);
    const std::uint32_t held = *reinterpret_cast<const std::uint32_t*>(state + 0x10);
    const bool was_waiting = cutscene_input_guard.waiting_for_neutral();
    const std::uint32_t filtered = cutscene_input_guard.filter(pressed, held);
    if (was_waiting && !cutscene_input_guard.waiting_for_neutral()) {
        log_line("character-selection action guard released after neutral input");
    } else if ((pressed & CutsceneInputGuard::action_mask) &&
               !(filtered & CutsceneInputGuard::action_mask)) {
        log_line("character-selection carried action suppressed held=%08X pressed=%08X",
                 held, pressed);
    }
    return filtered;
}

void __cdecl hooked_character_select_advance() {
    auto* image = static_cast<std::uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
    auto* selection_state = reinterpret_cast<std::uint32_t*>(image + (is_retail_executable() ? 0x102A6C : 0x102B64));
    const std::uint32_t state_before = *selection_state;
    std::uint32_t selected_item = 0;
    if (state_before == 2) {
        void* menu = *reinterpret_cast<void**>(image + (is_retail_executable() ? 0x102A64 : 0x102B5C));
        if (menu) selected_item = real_current_menu_item(menu);
    }
    real_character_select_advance();

    const std::uint32_t state_after = *selection_state;
    if (state_before != 2 || state_after != 3) return;

    auto* input_state = image + (is_retail_executable() ? 0x1BDB08 : 0x1BDC08);
    auto* pressed = reinterpret_cast<std::uint32_t*>(input_state + 8);
    const std::uint32_t carried = *pressed & CutsceneInputGuard::action_mask;
    *pressed &= ~CutsceneInputGuard::action_mask;
    cutscene_input_guard.consume_after_character_selection();
    log_line("character selection confirmed item=%04X state=%u->%u; consumed transition action=%08X",
             selected_item, state_before, state_after, carried);
    const int movie_id = CharacterSelectMovieGate::movie_for_item(selected_item);
    if (movie_id < 0) {
        log_line("FMV ledger serial=0 event=expected-rejected item=%04X reason=unknown-character",
                 selected_item);
        return;
    }

    char name[9]{};
    log_line("FMV ledger serial=0 event=selection-confirmed id=%d name=%s item=%04X stage=portrait-title",
             movie_id, movie_request_name(movie_id, name), selected_item);
}

int __cdecl hooked_post_selection_platform() {
    if (InterlockedCompareExchange(&post_selection_movie_in_flight, 1, 0) == 0) {
        auto* image = static_cast<std::uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
        const std::uint8_t character = *(image + (is_retail_executable() ? 0x101658 : 0x101758));
        const int movie_id = CharacterSelectMovieGate::movie_for_character_byte(character);
        auto* input_state = image + (is_retail_executable() ? 0x1BDB08 : 0x1BDC08);
        auto* pressed = reinterpret_cast<std::uint32_t*>(input_state + 8);
        const std::uint32_t carried = *pressed & CutsceneInputGuard::action_mask;
        *pressed &= ~CutsceneInputGuard::action_mask;
        cutscene_input_guard.consume_after_character_selection();

        const std::uint32_t expected_serial = movie_request_serial + 1;
        char name[9]{};
        log_line("FMV ledger serial=%u event=expected id=%d name=%s character=%u stage=post-portrait-title carried=%08X",
                 expected_serial, movie_id, movie_request_name(movie_id, name), character,
                 carried);
        // Dreamcast invokes SELECT_A/C with an authored skip mask of zero.
        hooked_movie_request(movie_id, 0);
        InterlockedExchange(&post_selection_movie_in_flight, 0);
    } else {
        log_line("post-selection movie request suppressed reason=reentrant");
    }
    return real_post_selection_platform();
}

bool install_cutscene_input_reuse_guard() {
#ifdef AITD4_TEST_HARNESS
    log_line("character-selection input reuse hooks skipped in test harness");
    return true;
#else
    auto* image = static_cast<std::uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
    const bool retail = is_retail_executable();
    auto* getter = image + (retail ? 0xA3CA2 : 0xA3E02);
    auto* selection_call = image + (retail ? 0x7FA6F : 0x7FCE5);
    auto* post_selection_call = image + (retail ? 0x79987 : 0xBAE94);
    auto* post_selection_context = image + (retail ? 0x79976 : 0xBAE83);
    auto* current_item = image + (retail ? 0xA8171 : 0xA82D1);
    auto* confirmation_branch = image + (retail ? 0x7FA98 : 0x7FD0E);
    constexpr std::uint8_t expected_getter[5]{0x55, 0x8B, 0xEC, 0x51, 0x89};
    constexpr std::uint8_t expected_selection_call_15_slot[5]{0xE8, 0x75, 0xAE, 0xFF, 0xFF};
    constexpr std::uint8_t expected_selection_call_retail[5]{0xE8, 0xCB, 0xAF, 0xFF, 0xFF};
    constexpr std::uint8_t expected_post_selection_context_15_slot[29]{
        0x8B, 0x4D, 0xF0, 0x81, 0xE1, 0xFF, 0x00, 0x00, 0x00, 0x85,
        0xC9, 0x0F, 0x84, 0xC1, 0xEC, 0xFB, 0xFF, 0xE8, 0x67, 0x8F,
        0xF4, 0xFF, 0x89, 0x45, 0xE4, 0x83, 0x7D, 0xE4, 0x01};
    constexpr std::uint8_t expected_post_selection_context_retail[29]{
        0x8B, 0x4D, 0xF0, 0x81, 0xE1, 0xFF, 0x00, 0x00, 0x00, 0x85,
        0xC9, 0x0F, 0x84, 0xA9, 0x00, 0x00, 0x00, 0xE8, 0x74, 0xA4,
        0xF8, 0xFF, 0x89, 0x45, 0xE4, 0x83, 0x7D, 0xE4, 0x01};
    constexpr std::uint8_t expected_current_item[8]{
        0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B};
    constexpr std::uint8_t expected_confirmation_branch_15_slot[16]{
        0x8B, 0x0D, 0x5C, 0x2B, 0x50, 0x00, 0xE8, 0xB8,
        0x85, 0x02, 0x00, 0x3D, 0x01, 0x20, 0x00, 0x00};
    constexpr std::uint8_t expected_confirmation_branch_retail[16]{
        0x8B, 0x0D, 0x64, 0x2A, 0x50, 0x00, 0xE8, 0xCE,
        0x86, 0x02, 0x00, 0x3D, 0x01, 0x20, 0x00, 0x00};
    if (std::memcmp(getter, expected_getter, sizeof(expected_getter)) != 0 ||
        std::memcmp(selection_call, retail ? expected_selection_call_retail : expected_selection_call_15_slot, 5) != 0 ||
        std::memcmp(post_selection_context, retail ? expected_post_selection_context_retail : expected_post_selection_context_15_slot, 29) != 0 ||
        std::memcmp(current_item, expected_current_item,
                    sizeof(expected_current_item)) != 0 ||
        std::memcmp(confirmation_branch, retail ? expected_confirmation_branch_retail : expected_confirmation_branch_15_slot, 16) != 0) {
        log_line("character-selection signature mismatch getter=%p confirm=%p post=%p item=%p branch=%p",
                 getter, selection_call, post_selection_call, current_item,
                 confirmation_branch);
        return false;
    }

    real_character_select_advance = reinterpret_cast<CharacterSelectAdvanceFn>(image + (retail ? 0x7AA3F : 0x7AB5F));
    real_post_selection_platform = reinterpret_cast<PostSelectionPlatformFn>(image + 0x3E00);
    real_current_menu_item = reinterpret_cast<CurrentMenuItemFn>(current_item);
    std::uint8_t getter_jump[5]{0xE9};
    const auto getter_displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(hooked_get_new_pressed) -
        (reinterpret_cast<std::intptr_t>(getter) + 5));
    std::memcpy(getter_jump + 1, &getter_displacement, sizeof(getter_displacement));

    std::uint8_t confirm_call[5]{0xE8};
    const auto confirm_displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(hooked_character_select_advance) -
        (reinterpret_cast<std::intptr_t>(selection_call) + 5));
    std::memcpy(confirm_call + 1, &confirm_displacement, sizeof(confirm_displacement));

    std::uint8_t post_selection_finish_call[5]{0xE8};
    const auto post_selection_finish_displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(hooked_post_selection_platform) -
        (reinterpret_cast<std::intptr_t>(post_selection_call) + 5));
    std::memcpy(post_selection_finish_call + 1, &post_selection_finish_displacement,
                sizeof(post_selection_finish_displacement));

    if (!write_code_patch(getter, getter_jump, sizeof(getter_jump)) ||
        !write_code_patch(selection_call, confirm_call, sizeof(confirm_call)) ||
        !write_code_patch(post_selection_call, post_selection_finish_call,
                          sizeof(post_selection_finish_call)))
        return false;
    log_line("character-selection deferred movie hooks installed getter=%p confirm=%p post=%p",
             getter, selection_call, post_selection_call);
    return true;
#endif
}

const char* movie_request_name(int id, char (&name)[9]) {
    if (id < 0 || id >= 64) return "<invalid>";
    auto* image = static_cast<std::uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
    const char* entry = reinterpret_cast<const char*>(image + (is_retail_executable() ? 0x103628 : 0x103728) + id * 10);
    std::memcpy(name, entry, 8);
    name[8] = '\0';
    return name[0] ? name : "<empty>";
}

void __cdecl hooked_movie_request(int id, std::uint32_t skip_mask) {
    const int previous_id = current_movie_request_id;
    const bool previous_opened = current_movie_request_opened;
    const std::uint32_t previous_serial = current_movie_request_serial;
    current_movie_request_id = id;
    current_movie_request_opened = false;
    const std::uint32_t serial = ++movie_request_serial;
    current_movie_request_serial = serial;
    char name[9]{};
    log_line("FMV ledger serial=%u event=request id=%d name=%s skip_mask=%08X", serial, id,
             movie_request_name(id, name), skip_mask);
    real_movie_request(id, skip_mask);
    log_line("FMV ledger serial=%u event=request-complete id=%d opened=%s", serial, id,
             current_movie_request_opened ? "yes" : "no");
    current_movie_request_id = previous_id;
    current_movie_request_opened = previous_opened;
    current_movie_request_serial = previous_serial;
}

bool install_movie_request_audit() {
#ifdef AITD4_TEST_HARNESS
    log_line("FMV request audit hook skipped in test harness");
    return true;
#else
    auto* image = static_cast<std::uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
    auto* target = image + (is_retail_executable() ? 0x810BF : 0x812CF);
    constexpr std::size_t prologue_size = 9;
    constexpr std::uint8_t expected[prologue_size]{
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x94, 0x00, 0x00, 0x00};
    if (std::memcmp(target, expected, sizeof(expected)) != 0) {
        log_line("FMV request hook signature mismatch address=%p", target);
        return false;
    }

    auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, prologue_size + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) return false;
    std::memcpy(trampoline, target, prologue_size);
    trampoline[prologue_size] = 0xE9;
    const auto return_displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(target + prologue_size) -
        (reinterpret_cast<std::intptr_t>(trampoline + prologue_size) + 5));
    std::memcpy(trampoline + prologue_size + 1, &return_displacement,
                sizeof(return_displacement));
    FlushInstructionCache(GetCurrentProcess(), trampoline, prologue_size + 5);
    real_movie_request = reinterpret_cast<MovieRequestFn>(trampoline);

    std::uint8_t replacement[prologue_size]{0xE9};
    const auto hook_displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(hooked_movie_request) -
        (reinterpret_cast<std::intptr_t>(target) + 5));
    std::memcpy(replacement + 1, &hook_displacement, sizeof(hook_displacement));
    std::fill(replacement + 5, replacement + prologue_size,
              static_cast<std::uint8_t>(0x90));
    if (!write_code_patch(target, replacement, sizeof(replacement))) return false;
    log_line("FMV request audit hook installed address=%p trampoline=%p", target, trampoline);
    return true;
#endif
}

void* WINAPI hooked_bink_buffer_open(HWND window, std::uint32_t width, std::uint32_t height,
                                     std::uint32_t flags) {
    void* buffer = real_bink_buffer_open(window, width, height, flags);
    last_bink_source_width = width;
    last_bink_source_height = height;
    last_bink_output_width = static_cast<std::uint32_t>(MulDiv(
        static_cast<int>(width), output_viewport.width, 640));
    last_bink_output_height = static_cast<std::uint32_t>(MulDiv(
        static_cast<int>(height), output_viewport.height, 480));
    if (buffer) {
        active_bink_buffer = buffer;
        active_bink_canvas_x = std::max(0, (640 - static_cast<int>(width)) / 2);
        active_bink_canvas_y = std::max(0, (480 - static_cast<int>(height)) / 2);
        last_bink_destination = nullptr;
        last_bink_pitch = 0;
        last_bink_copy_height = 0;
        last_bink_copy_flags = 0;
        last_bink_copy_x = 0;
        last_bink_copy_y = 0;
        artificial_bink_lock = false;
        if (!g_config.crt_enabled &&
            !real_bink_buffer_set_scale(buffer, last_bink_output_width, last_bink_output_height))
            fatal_graphics("Native Bink rejected the proportional FMV scale. The game will not continue with misaligned video.");
        log_line("Bink movie opened buffer=%p source=%ux%u output=%ux%u presentation=%s",
                 buffer, width, height, last_bink_output_width, last_bink_output_height,
                 g_config.crt_enabled ? "native-canvas-crt" : "native-scaled-blit");
    }
    return buffer;
}

int WINAPI hooked_bink_buffer_set_offset(void* buffer, int x, int y) {
    if (buffer == active_bink_buffer) {
        active_bink_canvas_x = x;
        active_bink_canvas_y = y;
    }
    last_bink_output_x = output_viewport.x + MulDiv(x, output_viewport.width, 640);
    last_bink_output_y = output_viewport.y + MulDiv(y, output_viewport.height, 480);
    log_line("Bink movie offset buffer=%p source=%d,%d output=%d,%d", buffer, x, y,
             last_bink_output_x, last_bink_output_y);
    return g_config.crt_enabled
        ? real_bink_buffer_set_offset(buffer, x, y)
        : real_bink_buffer_set_offset(buffer, last_bink_output_x, last_bink_output_y);
}

void* WINAPI hooked_bink_open(const char* filename, std::uint32_t flags) {
    const bool readable_name = filename && !IsBadStringPtrA(filename, MAX_PATH);
    void* bink = real_bink_open(filename, flags);
    const char* error = !bink && real_bink_get_error ? real_bink_get_error() : nullptr;
    log_line("BinkOpen file=%s pointer=%p flags=%08X result=%p error=%s",
             readable_name ? filename : "<unreadable>", filename, flags, bink,
             error ? error : "-");
    // alone4.exe intentionally retries BinkOpen with an alternate path after a null result.
    // Preserve that native control flow; the log records both attempts and the final Bink error.
    if (bink) InterlockedExchange(&fmv_pixel_trace_budget, 256);
    if (bink) {
        current_movie_request_opened = true;
        movie_skip_gate.reset();
        active_bink_movie = bink;
        active_bink_request_id = current_movie_request_id;
        active_bink_request_serial = current_movie_request_serial;
        active_bink_do_frames = 0;
        active_bink_copies = 0;
        active_bink_next_frames = 0;
        active_bink_presented_frames = 0;
        active_bink_suppressed_blits = 0;
        active_bink_buffer = nullptr;
        artificial_bink_lock = false;
        alternate_bink_frame_pending = false;
        last_bink_source_width = 0;
        last_bink_source_height = 0;
        last_bink_output_width = 0;
        last_bink_output_height = 0;
        last_bink_destination = nullptr;
        last_bink_pitch = 0;
        last_bink_copy_height = 0;
        last_bink_copy_flags = 0;
        log_line("FMV ledger serial=%u event=open id=%d movie=%p",
                 active_bink_request_serial, active_bink_request_id, bink);
    }
    return bink;
}

std::uint32_t __fastcall hooked_movie_skip_input(void* input_state, void*) {
    const auto* state = static_cast<const std::uint8_t*>(input_state);
    const std::uint32_t pressed = *reinterpret_cast<const std::uint32_t*>(state + 8);
    const std::uint32_t held = *reinterpret_cast<const std::uint32_t*>(state + 0x10);
    const bool was_armed = movie_skip_gate.armed();
    const std::uint32_t filtered = movie_skip_gate.filter(pressed, held);
    if (!was_armed && movie_skip_gate.armed())
        log_line("movie skip gate armed after neutral input held=%08X pressed=%08X", held, pressed);
    else if ((pressed & MovieSkipGate::skip_mask) &&
             !(filtered & MovieSkipGate::skip_mask))
        log_line("movie skip edge suppressed without a held-state rising edge held=%08X pressed=%08X", held, pressed);
    else if (was_armed && (filtered & MovieSkipGate::skip_mask))
        log_line("movie skip edge accepted held=%08X pressed=%08X", held, pressed);
    return filtered;
}

bool install_movie_skip_gate() {
#ifdef AITD4_TEST_HARNESS
    log_line("movie skip callsite hook skipped in test harness");
    return true;
#else
    auto* image = static_cast<std::uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
    auto* call = image + (is_retail_executable() ? 0x9DD7F : 0x9DEDF);
    constexpr std::uint8_t expected[5]{0xE8, 0x1E, 0x5F, 0x00, 0x00};
    if (std::memcmp(call, expected, sizeof(expected)) != 0) {
        log_line("movie skip callsite signature mismatch address=%p", call);
        return false;
    }
    std::uint8_t replacement[5]{0xE8};
    const auto displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(hooked_movie_skip_input) -
        (reinterpret_cast<std::intptr_t>(call) + 5));
    std::memcpy(replacement + 1, &displacement, sizeof(displacement));
    DWORD old_protection = 0;
    if (!VirtualProtect(call, sizeof(replacement), PAGE_EXECUTE_READWRITE, &old_protection))
        return false;
    std::memcpy(call, replacement, sizeof(replacement));
    DWORD ignored = 0;
    VirtualProtect(call, sizeof(replacement), old_protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), call, sizeof(replacement));
    log_line("movie skip release-gate hook installed address=%p", call);
    return true;
#endif
}

std::uint32_t WINAPI hooked_bink_do_frame(void* bink) {
    const std::uint32_t result = real_bink_do_frame(bink);
    if (bink == active_bink_movie) {
        ++active_bink_do_frames;
        if (active_bink_do_frames == 1)
            log_line("FMV ledger serial=%u event=first-frame id=%d movie=%p result=%u",
                     active_bink_request_serial, active_bink_request_id, bink, result);
        if (active_bink_do_frames <= 8 || active_bink_do_frames % 120 == 0)
            log_line("BinkDoFrame movie=%p count=%u result=%u", bink,
                     active_bink_do_frames, result);
    }
    return result;
}

std::uint32_t WINAPI hooked_bink_copy_to_buffer(
    void* bink, void* destination, std::int32_t pitch, std::uint32_t height,
    std::uint32_t x, std::uint32_t y, std::uint32_t flags) {
    if (!g_config.crt_enabled)
        return real_bink_copy_to_buffer(bink, destination, pitch, height, x, y, flags);
    if (bink != active_bink_movie || pitch == 0 || height == 0) {
        log_line("CRT Bink copy precondition rejected movie=%p active_movie=%p active_buffer=%p destination=%p pitch=%d height=%u offset=%u,%u flags=%08X",
                 bink, active_bink_movie, active_bink_buffer, destination, pitch, height,
                 x, y, flags);
        fatal_graphics("CRT Bink decoding encountered an untracked movie or invalid destination geometry.");
    }
    if (last_bink_source_width == 0) {
        if (IsBadReadPtr(bink, sizeof(std::uint32_t)))
            fatal_graphics("CRT Bink decoding could not read the native movie width.");
        last_bink_source_width = *static_cast<const std::uint32_t*>(bink);
        last_bink_source_height = height;
        active_bink_canvas_x = std::max(0, (640 - static_cast<int>(last_bink_source_width)) / 2);
        active_bink_canvas_y = std::max(0, (480 - static_cast<int>(height)) / 2);
        last_bink_output_width = static_cast<std::uint32_t>(MulDiv(
            static_cast<int>(last_bink_source_width), output_viewport.width, 640));
        last_bink_output_height = static_cast<std::uint32_t>(MulDiv(
            static_cast<int>(height), output_viewport.height, 480));
        last_bink_output_x = output_viewport.x +
            MulDiv(active_bink_canvas_x, output_viewport.width, 640);
        last_bink_output_y = output_viewport.y +
            MulDiv(active_bink_canvas_y, output_viewport.height, 480);
        log_line("Bink geometry recovered from decoded frame source=%ux%u canvas=%d,%d output=%ux%u at %d,%d",
                 last_bink_source_width, height, active_bink_canvas_x, active_bink_canvas_y,
                 last_bink_output_width, last_bink_output_height,
                 last_bink_output_x, last_bink_output_y);
    }
    if (last_bink_source_width == 0 || last_bink_source_width > 4096 ||
        std::abs(static_cast<long long>(pitch)) <
            static_cast<long long>(last_bink_source_width) * 3) {
        fatal_graphics("CRT Bink decoding rejected the native movie width or pitch.");
    }
    const auto row_bytes = static_cast<std::size_t>(
        std::abs(static_cast<long long>(pitch)));
    if (row_bytes > 16 * 1024 * 1024 || height > 4096 ||
        row_bytes * static_cast<std::size_t>(height) > 64 * 1024 * 1024) {
        fatal_graphics("CRT Bink decoding refused an unreasonable destination buffer size.");
    }
    if (!active_bink_buffer) {
        if (alternate_bink_frame_pending)
            fatal_graphics("The native OpenGL Bink backend decoded a second frame before presenting the first.");
        const std::uint32_t result = real_bink_copy_to_buffer(
            bink, destination, pitch, height, x, y, flags);
        ++active_bink_copies;
        last_bink_destination = static_cast<const std::uint8_t*>(destination);
        last_bink_pitch = pitch;
        last_bink_copy_height = height;
        last_bink_copy_flags = flags;
        last_bink_copy_x = x;
        last_bink_copy_y = y;
        alternate_bink_frame_pending = true;
        if (active_bink_copies <= 8 || active_bink_copies % 120 == 0)
            log_line("BinkCopyToBuffer native-OpenGL movie=%p count=%u destination=%p pitch=%d height=%u offset=%u,%u flags=%08X result=%u",
                     bink, active_bink_copies, destination, pitch, height, x, y, flags,
                     result);
        return result;
    }
    bink_decode_storage.assign(row_bytes * height, 0);
    auto* decode_destination = bink_decode_storage.data();
    if (pitch < 0) decode_destination += row_bytes * (height - 1);
    const std::uint32_t result = real_bink_copy_to_buffer(
        bink, decode_destination, pitch, height, x, y, flags);
    ++active_bink_copies;
    last_bink_destination = decode_destination;
    last_bink_pitch = pitch;
    last_bink_copy_height = height;
    last_bink_copy_flags = flags;
    last_bink_copy_x = x;
    last_bink_copy_y = y;
    if (active_bink_copies <= 8 || active_bink_copies % 120 == 0)
        log_line("BinkCopyToBuffer redirected movie=%p count=%u native_destination=%p decode_destination=%p pitch=%d height=%u offset=%u,%u flags=%08X result=%u",
                 bink, active_bink_copies, destination, decode_destination, pitch, height, x, y,
                 flags, result);
    return result;
}

void present_decoded_bink_frame(void* buffer) {
    if (!crt_ready || !framebuffer_ready || buffer != active_bink_buffer ||
        !last_bink_destination || last_bink_source_width == 0 || last_bink_copy_height == 0 ||
        last_bink_copy_x != 0 || last_bink_copy_y != 0 ||
        wglGetCurrentContext() != game_context) {
        fatal_graphics("CRT movie presentation was requested without a complete current OpenGL/Bink frame. Native blitting was not used as a fallback.");
    }
    if (!copy_bink_frame_to_rgba_canvas(
            {last_bink_destination, last_bink_pitch,
             static_cast<int>(last_bink_source_width), static_cast<int>(last_bink_copy_height),
             last_bink_copy_flags},
            640, 480, active_bink_canvas_x, active_bink_canvas_y, bink_canvas_rgba)) {
        log_line("unsupported Bink frame buffer=%p destination=%p size=%ux%u pitch=%d copy=%u,%u canvas=%d,%d flags=%08X",
                 buffer, last_bink_destination, last_bink_source_width, last_bink_copy_height,
                 last_bink_pitch, last_bink_copy_x, last_bink_copy_y,
                 active_bink_canvas_x, active_bink_canvas_y,
                 last_bink_copy_flags);
        fatal_graphics("The decoded Bink frame format or placement is unsupported. Native blitting was not used as a fallback.");
    }

    inside_compositor = true;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    GLint previous_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_POLYGON_STIPPLE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_POLYGON_OFFSET_LINE);
    glDisable(GL_POLYGON_OFFSET_POINT);
    glDisable(GL_FOG);
    glDisable(GL_LIGHTING);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_CLIP_PLANE0);
    glDisable(GL_CLIP_PLANE1);
    glDisable(GL_CLIP_PLANE2);
    glDisable(GL_CLIP_PLANE3);
    glDisable(GL_CLIP_PLANE4);
    glDisable(GL_CLIP_PLANE5);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    active_texture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, bink_canvas_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    while (glGetError() != GL_NO_ERROR) {}
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE,
                    bink_canvas_rgba.data());
    if (glGetError() != GL_NO_ERROR)
        fatal_graphics("The decoded Bink frame could not be uploaded to the CRT canvas.");
    if (!present_crt_texture(bink_canvas_texture, 640, 480, g_config.deband))
        fatal_graphics("CRT movie composition failed. Native blitting was not used as a fallback.");

    DWORD capture_requests = static_cast<DWORD>(InterlockedExchange(&capture_request_flags, 0));
    if (g_config.development_capture && (GetAsyncKeyState(VK_F10) & 1))
        capture_requests |= renderer_capture_output;
    if (capture_requests & renderer_capture_output)
        capture_frame("output", 0, GL_BACK, monitor_rect.right - monitor_rect.left,
                      monitor_rect.bottom - monitor_rect.top);

    use_program(static_cast<GLuint>(previous_program));
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
    HDC dc = GetDC(game_window);
    const BOOL presented = dc ? real_swap_buffers(dc) : FALSE;
    if (dc) ReleaseDC(game_window, dc);
    bind_framebuffer(GL_FRAMEBUFFER, multisample_fbo);
    inside_compositor = false;
    if (!presented)
        fatal_graphics("CRT movie SwapBuffers failed. Native blitting was not used as a fallback.");

    ++bink_presented_frames;
    ++bink_native_blits_suppressed;
    ++active_bink_presented_frames;
    ++active_bink_suppressed_blits;
    if (active_bink_presented_frames <= 8 || active_bink_presented_frames % 120 == 0)
        log_line("Bink frame explicit-present buffer=%p presented=%u format=%u pitch=%d",
                 buffer, active_bink_presented_frames,
                 last_bink_copy_flags & bink_surface_mask, last_bink_pitch);
}

void WINAPI hooked_bink_buffer_unlock(void* buffer) {
    if (!g_config.crt_enabled) {
        real_bink_buffer_unlock(buffer);
        return;
    }
    if (buffer != active_bink_buffer)
        fatal_graphics("CRT Bink unlock encountered an untracked presentation buffer.");
    if (!artificial_bink_lock)
        fatal_graphics("CRT Bink unlock encountered a buffer that was not logically locked.");
    artificial_bink_lock = false;
    if (active_bink_presented_frames == active_bink_copies) {
        log_line("BinkBufferUnlock ignored buffer=%p reason=no-new-decoded-frame", buffer);
        return;
    }
    if (active_bink_presented_frames + 1 != active_bink_copies)
        fatal_graphics("CRT Bink frame lifecycle lost synchronization before presentation.");
    present_decoded_bink_frame(buffer);
}

int WINAPI hooked_bink_buffer_lock(void* buffer) {
    if (!g_config.crt_enabled) return real_bink_buffer_lock(buffer);
    if (!active_bink_buffer) active_bink_buffer = buffer;
    if (active_bink_copies < 8)
        log_line("BinkBufferLock logical buffer=%p active=%p locked=%d",
                 buffer, active_bink_buffer, artificial_bink_lock ? 1 : 0);
    if (buffer != active_bink_buffer || artificial_bink_lock)
        fatal_graphics("CRT Bink lock encountered an invalid or already locked presentation buffer.");
    artificial_bink_lock = true;
    return 1;
}

bool install_native_movie_frame_hook() {
#ifdef AITD4_TEST_HARNESS
    log_line("native Bink movie-frame callsite hook skipped in test harness");
    return true;
#else
    auto* image = static_cast<std::uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
    auto* call = image + (is_retail_executable() ? 0x9DDD7 : 0x9DF37);
    auto* native_frame = image + (is_retail_executable() ? 0x9DF42 : 0x9E0A2);
    constexpr std::uint8_t expected_call[5]{0xE8, 0x66, 0x01, 0x00, 0x00};
    constexpr std::uint8_t expected_frame_prefix[23]{
        0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x50, 0xFF, 0x15, 0x38, 0xB2, 0x4B,
        0x00, 0x8B, 0x4D, 0x0C, 0x51, 0xFF, 0x15, 0x2C, 0xB2, 0x4B, 0x00};
    constexpr std::uint8_t expected_frame_tail[18]{
        0x8B, 0x55, 0x08, 0x8B, 0x45, 0x08, 0x8B, 0x4A, 0x0C,
        0x3B, 0x48, 0x08, 0x74, 0x0A, 0x8B, 0x55, 0x08, 0x52};
    if (std::memcmp(call, expected_call, sizeof(expected_call)) != 0 ||
        std::memcmp(native_frame, expected_frame_prefix, sizeof(expected_frame_prefix)) != 0 ||
        std::memcmp(native_frame + 0x72, expected_frame_tail, sizeof(expected_frame_tail)) != 0) {
        log_line("native Bink movie-frame signature mismatch call=%p frame=%p", call,
                 native_frame);
        return false;
    }
    real_native_movie_frame = reinterpret_cast<NativeMovieFrameFn>(native_frame);
    std::uint8_t replacement[5]{0xE8};
    const auto displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(hooked_native_movie_frame) -
        (reinterpret_cast<std::intptr_t>(call) + 5));
    std::memcpy(replacement + 1, &displacement, sizeof(displacement));
    if (!write_code_patch(call, replacement, sizeof(replacement))) return false;
    log_line("native Bink movie-frame lifecycle hooked call=%p original=%p", call,
             native_frame);
    return true;
#endif
}

void __cdecl hooked_native_movie_frame(void* bink, void* buffer) {
    if (!g_config.crt_enabled) {
        if (!real_native_movie_frame)
            fatal_graphics("The native Bink movie-frame routine is unavailable.");
        real_native_movie_frame(bink, buffer);
        return;
    }
    if (!bink || bink != active_bink_movie || !buffer ||
        IsBadReadPtr(bink, 0x10) || IsBadReadPtr(buffer, 0x1C)) {
        log_line("CRT native movie-frame precondition rejected movie=%p active_movie=%p buffer=%p",
                 bink, active_bink_movie, buffer);
        fatal_graphics("CRT movie presentation received invalid native movie state.");
    }
    if (active_bink_buffer && active_bink_buffer != buffer)
        fatal_graphics("CRT movie presentation changed native Bink buffers during playback.");
    active_bink_buffer = buffer;

    hooked_bink_do_frame(bink);
    const int locked = real_bink_buffer_lock(buffer);
    bool decoded = false;
    if (locked) {
        auto* bytes = static_cast<std::uint8_t*>(buffer);
        const std::uint32_t height = *reinterpret_cast<const std::uint32_t*>(bytes + 0x04);
        const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(bytes + 0x10);
        void* destination = *reinterpret_cast<void* const*>(bytes + 0x14);
        const std::int32_t pitch = *reinterpret_cast<const std::int32_t*>(bytes + 0x18);
        hooked_bink_copy_to_buffer(bink, destination, pitch, height, 0, 0, flags);
        real_bink_buffer_unlock(buffer);
        decoded = true;
    }

    auto* movie_bytes = static_cast<const std::uint8_t*>(bink);
    const std::uint32_t frame = *reinterpret_cast<const std::uint32_t*>(movie_bytes + 0x0C);
    const std::uint32_t frames = *reinterpret_cast<const std::uint32_t*>(movie_bytes + 0x08);
    const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(
        static_cast<const std::uint8_t*>(buffer) + 0x10);
    // Preserve the native dirty-rectangle query even though its direct window blit is suppressed.
    real_bink_get_rects(bink, flags);
    if (decoded) {
        // This is the exact point where the native routine would issue BinkBufferBlit.
        present_decoded_bink_frame(buffer);
    } else {
        ++bink_native_blits_suppressed;
        ++active_bink_suppressed_blits;
        log_line("native Bink buffer lock returned no frame movie=%p buffer=%p", bink, buffer);
    }
    if (frame != frames) hooked_bink_next_frame(bink);
}

#ifdef AITD4_TEST_HARNESS
extern "C" __declspec(dllexport) DWORD WINAPI AITD4_TestNativeMovieFrame(
    void* bink, void* buffer) {
    hooked_native_movie_frame(bink, buffer);
    return 1;
}
#endif

void WINAPI hooked_bink_buffer_blit(void* buffer, void* rectangles,
                                    std::uint32_t rectangle_count) {
    if (!g_config.crt_enabled) {
        real_bink_buffer_blit(buffer, rectangles, rectangle_count);
        return;
    }
    if (buffer != active_bink_buffer)
        fatal_graphics("CRT Bink blit encountered an untracked presentation buffer.");
    log_line("BinkBufferBlit suppressed after explicit unlock presentation buffer=%p rectangles=%p count=%u",
             buffer, rectangles, rectangle_count);
}

void WINAPI hooked_bink_next_frame(void* bink) {
    real_bink_next_frame(bink);
    if (bink == active_bink_movie) {
        ++active_bink_next_frames;
        if (active_bink_next_frames <= 8 || active_bink_next_frames % 120 == 0)
            log_line("BinkNextFrame movie=%p count=%u", bink, active_bink_next_frames);
    }
}

void WINAPI hooked_bink_close(void* bink) {
    if (bink == active_bink_movie) {
        log_line("FMV ledger serial=%u event=close id=%d movie=%p",
                 active_bink_request_serial, active_bink_request_id, bink);
        log_line("FMV ledger serial=%u event=frames id=%d do=%u copy=%u next=%u presented=%u suppressed=%u",
                  active_bink_request_serial, active_bink_request_id, active_bink_do_frames,
                  active_bink_copies, active_bink_next_frames, active_bink_presented_frames,
                  active_bink_suppressed_blits);
        log_line("BinkClose movie=%p request_id=%d do_frames=%u copies=%u next_frames=%u",
                 bink, active_bink_request_id, active_bink_do_frames, active_bink_copies,
                 active_bink_next_frames);
        active_bink_movie = nullptr;
        active_bink_request_id = -1;
        active_bink_request_serial = 0;
        active_bink_buffer = nullptr;
        last_bink_destination = nullptr;
        artificial_bink_lock = false;
        alternate_bink_frame_pending = false;
    } else {
        log_line("BinkClose movie=%p untracked", bink);
    }
    real_bink_close(bink);
}

bool install_bink_hooks() {
    HMODULE bink = GetModuleHandleA("binkw32.dll");
#ifdef AITD4_TEST_HARNESS
    if (!bink) {
        log_line("Bink integration skipped in non-Bink test harness");
        return true;
    }
#endif
    if (!bink) {
        log_line("required native Bink module is unavailable");
        return false;
    }
    real_bink_buffer_set_scale = reinterpret_cast<BinkBufferSetScaleFn>(
        ::GetProcAddress(bink, "_BinkBufferSetScale@12"));
    real_bink_get_error = reinterpret_cast<BinkGetErrorFn>(
        ::GetProcAddress(bink, "_BinkGetError@0"));
    real_bink_get_rects = reinterpret_cast<BinkGetRectsFn>(
        ::GetProcAddress(bink, "_BinkGetRects@8"));
    bool ok = real_bink_buffer_set_scale != nullptr && real_bink_get_error != nullptr &&
              real_bink_get_rects != nullptr;
    ok &= patch_iat("binkw32.dll", "_BinkOpen@8",
                    reinterpret_cast<void*>(hooked_bink_open),
                    reinterpret_cast<void**>(&real_bink_open));
    ok &= patch_iat("binkw32.dll", "_BinkDoFrame@4",
                    reinterpret_cast<void*>(hooked_bink_do_frame),
                    reinterpret_cast<void**>(&real_bink_do_frame));
    ok &= patch_iat("binkw32.dll", "_BinkCopyToBuffer@28",
                    reinterpret_cast<void*>(hooked_bink_copy_to_buffer),
                    reinterpret_cast<void**>(&real_bink_copy_to_buffer));
    ok &= patch_iat("binkw32.dll", "_BinkBufferBlit@12",
                    reinterpret_cast<void*>(hooked_bink_buffer_blit),
                    reinterpret_cast<void**>(&real_bink_buffer_blit));
    ok &= patch_iat("binkw32.dll", "_BinkBufferLock@4",
                    reinterpret_cast<void*>(hooked_bink_buffer_lock),
                    reinterpret_cast<void**>(&real_bink_buffer_lock));
    ok &= patch_iat("binkw32.dll", "_BinkBufferUnlock@4",
                    reinterpret_cast<void*>(hooked_bink_buffer_unlock),
                    reinterpret_cast<void**>(&real_bink_buffer_unlock));
    ok &= patch_iat("binkw32.dll", "_BinkNextFrame@4",
                    reinterpret_cast<void*>(hooked_bink_next_frame),
                    reinterpret_cast<void**>(&real_bink_next_frame));
    ok &= patch_iat("binkw32.dll", "_BinkClose@4",
                    reinterpret_cast<void*>(hooked_bink_close),
                    reinterpret_cast<void**>(&real_bink_close));
    ok &= patch_iat("binkw32.dll", "_BinkBufferOpen@16",
                    reinterpret_cast<void*>(hooked_bink_buffer_open),
                    reinterpret_cast<void**>(&real_bink_buffer_open));
    ok &= patch_iat("binkw32.dll", "_BinkBufferSetOffset@12",
                    reinterpret_cast<void*>(hooked_bink_buffer_set_offset),
                    reinterpret_cast<void**>(&real_bink_buffer_set_offset));
    ok &= install_native_movie_frame_hook();
    bink_hooks_ready = ok;
    log_line("Bink proportional movie hooks %s", ok ? "ready" : "failed");
    return ok;
}

int WINAPI hooked_choose_pixel_format(HDC dc, const PIXELFORMATDESCRIPTOR* descriptor) {
    if (!descriptor || !g_config.fix_color_depth || WindowFromDC(dc) != game_window)
        return real_choose_pixel_format(dc, descriptor);
    PIXELFORMATDESCRIPTOR desired = *descriptor;
    desired.nSize = sizeof(desired);
    desired.nVersion = 1;
    desired.dwFlags |= PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    desired.iPixelType = PFD_TYPE_RGBA;
    desired.cColorBits = 32;
    desired.cAlphaBits = 8;
    desired.cDepthBits = 24;
    desired.cStencilBits = 8;
    const int format = real_choose_pixel_format(dc, &desired);
    if (!pixel_format_logged) {
        log_line("pixel format requested rgba8 depth24 stencil8 double-buffered result=%d", format);
        pixel_format_logged = true;
    }
    return format;
}

HGLRC WINAPI hooked_wgl_create_context(HDC dc) {
    if (!real_wgl_create_context)
        fatal_graphics("The original OpenGL context entry point is unavailable.");
    const HGLRC bootstrap = real_wgl_create_context(dc);
    if (!bootstrap || !wglMakeCurrent(dc, bootstrap))
        fatal_graphics("Unable to create the temporary OpenGL compatibility context.");
    auto create_attribs = reinterpret_cast<WglCreateContextAttribsFn>(
        wglGetProcAddress("wglCreateContextAttribsARB"));
    if (!create_attribs) {
        wglMakeCurrent(nullptr, nullptr);
        if (real_wgl_delete_context) real_wgl_delete_context(bootstrap);
        fatal_graphics("WGL_ARB_create_context is unavailable; OpenGL 3.3 compatibility is required.");
    }
    const int attributes[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
        0
    };
    const HGLRC compatibility = create_attribs(dc, nullptr, attributes);
    wglMakeCurrent(nullptr, nullptr);
    if (real_wgl_delete_context)
        real_wgl_delete_context(bootstrap);
    else
        wglDeleteContext(bootstrap);
    if (!compatibility)
        fatal_graphics("The display driver refused an OpenGL 3.3 compatibility context.");
    PIXELFORMATDESCRIPTOR actual{};
    actual.nSize = sizeof(actual);
    actual.nVersion = 1;
    const int format = GetPixelFormat(dc);
    if (format > 0 && DescribePixelFormat(dc, format, sizeof(actual), &actual)) {
        actual_color_bits = actual.cColorBits;
        actual_alpha_bits = actual.cAlphaBits;
        actual_depth_bits = actual.cDepthBits;
        actual_stencil_bits = actual.cStencilBits;
        log_line("pixel format actual color=%d alpha=%d depth=%d stencil=%d flags=%08lX",
                 actual_color_bits, actual_alpha_bits, actual_depth_bits, actual_stencil_bits,
                 actual.dwFlags);
        if (g_config.fix_color_depth &&
            (actual_color_bits < 32 || actual_alpha_bits < 8 || actual_depth_bits < 24 ||
             actual_stencil_bits < 8))
            fatal_graphics("The selected pixel format does not provide RGBA8 with depth-24/stencil-8.");
    }
    log_line("explicit OpenGL 3.3 compatibility context created context=%p", compatibility);
    return compatibility;
}

BOOL WINAPI hooked_wgl_make_current(HDC dc, HGLRC context) {
    const BOOL result = real_wgl_make_current(dc, context);
    if (result && context && WindowFromDC(dc) == game_window) {
        if (framebuffer_ready && game_context != context) {
            log_line("OpenGL context changed old=%p new=%p; rebuilding compositor resources",
                     game_context, context);
            reset_framebuffer_state(false);
        }
        if (!framebuffer_ready && !initialize_framebuffer())
            fatal_graphics("OpenGL 3.3 framebuffer initialization failed. The game will not run partially patched.");
    }
    return result;
}

BOOL WINAPI hooked_wgl_delete_context(HGLRC context) {
    if (framebuffer_ready && context == game_context) {
        const bool can_destroy = wglGetCurrentContext() == context;
        log_line("OpenGL game context deleted context=%p destroy_objects=%d",
                 context, can_destroy ? 1 : 0);
        reset_framebuffer_state(can_destroy);
    }
    return real_wgl_delete_context(context);
}

GLuint compile_stage(GLenum type, const char* source) {
    GLuint shader = create_shader(type);
    shader_source(shader, 1, &source, nullptr);
    compile_shader(shader);
    GLint ok = 0;
    get_shader_iv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[2048]{};
        GLsizei length = 0;
        get_shader_info_log(shader, sizeof(info), &length, info);
        log_line("shader compile failed: %s", info);
        return 0;
    }
    return shader;
}

std::string read_shader_override(const char* name) {
    char path[MAX_PATH]{};
    std::snprintf(path, MAX_PATH, "%s\\shaders\\%s", g_module_directory, name);
    FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || !file) return {};
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return {};
    }
    const long size = std::ftell(file);
    if (size <= 0 || size > 1024 * 1024 || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return {};
    }
    std::string source(static_cast<std::size_t>(size), '\0');
    const std::size_t read = std::fread(source.data(), 1, source.size(), file);
    std::fclose(file);
    if (read != source.size()) return {};
    return source;
}

bool create_compositor_program() {
    static constexpr char vertex_source[] = R"GLSL(
#version 120
varying vec2 textureCoordinate;
void main() {
    gl_Position = gl_Vertex;
    textureCoordinate = gl_MultiTexCoord0.xy;
}
)GLSL";
    static constexpr char fragment_source[] = R"GLSL(
#version 120
uniform sampler2D sourceTexture;
uniform vec2 inverseSize;
uniform float debandAmount;
uniform float ditherAmount;
varying vec2 textureCoordinate;

float hashNoise(vec2 position) {
    return fract(52.9829189 * fract(dot(position, vec2(0.06711056, 0.00583715))));
}

float blueNoise(vec2 position) {
    float center = hashNoise(position);
    float lowFrequency = (hashNoise(position + vec2(1.0, 0.0)) +
                          hashNoise(position - vec2(1.0, 0.0)) +
                          hashNoise(position + vec2(0.0, 1.0)) +
                          hashNoise(position - vec2(0.0, 1.0))) * 0.25;
    return clamp((center - lowFrequency) * 0.625, -0.5, 0.5);
}

void main() {
    vec3 center = texture2D(sourceTexture, textureCoordinate).rgb;
    vec3 leftColor = texture2D(sourceTexture, textureCoordinate - vec2(inverseSize.x, 0.0)).rgb;
    vec3 rightColor = texture2D(sourceTexture, textureCoordinate + vec2(inverseSize.x, 0.0)).rgb;
    vec3 downColor = texture2D(sourceTexture, textureCoordinate - vec2(0.0, inverseSize.y)).rgb;
    vec3 upColor = texture2D(sourceTexture, textureCoordinate + vec2(0.0, inverseSize.y)).rgb;
    vec3 averageColor = (leftColor + rightColor + downColor + upColor) * 0.25;
    vec3 localRange = max(max(abs(center - leftColor), abs(center - rightColor)),
                          max(abs(center - downColor), abs(center - upColor)));
    float difference = max(max(localRange.r, localRange.g), localRange.b);
    float gradientWeight = 1.0 - smoothstep(1.0 / 255.0, 4.0 / 255.0, difference);
    center = mix(center, averageColor, gradientWeight * 0.18 * debandAmount);
    center += blueNoise(gl_FragCoord.xy) * (0.75 / 255.0) * ditherAmount;
    gl_FragColor = vec4(clamp(center, 0.0, 1.0), 1.0);
}
)GLSL";
    std::string vertex_override;
    std::string fragment_override;
    const char* selected_vertex = vertex_source;
    const char* selected_fragment = fragment_source;
    if (g_config.development_hot_reload) {
        vertex_override = read_shader_override("compositor.vert");
        fragment_override = read_shader_override("compositor.frag");
        if (!vertex_override.empty()) selected_vertex = vertex_override.c_str();
        if (!fragment_override.empty()) selected_fragment = fragment_override.c_str();
    }
    const GLuint vertex = compile_stage(GL_VERTEX_SHADER, selected_vertex);
    const GLuint fragment = compile_stage(GL_FRAGMENT_SHADER, selected_fragment);
    if (!vertex || !fragment) {
        if (vertex) delete_shader(vertex);
        if (fragment) delete_shader(fragment);
        return false;
    }
    const GLuint candidate = create_program();
    attach_shader(candidate, vertex);
    attach_shader(candidate, fragment);
    link_program(candidate);
    delete_shader(vertex);
    delete_shader(fragment);
    GLint ok = 0;
    get_program_iv(candidate, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[2048]{};
        GLsizei length = 0;
        get_program_info_log(candidate, sizeof(info), &length, info);
        log_line("shader link failed: %s", info);
        delete_program(candidate);
        return false;
    }
    const GLint candidate_source = get_uniform_location(candidate, "sourceTexture");
    const GLint candidate_inverse_size = get_uniform_location(candidate, "inverseSize");
    const GLint candidate_deband = get_uniform_location(candidate, "debandAmount");
    const GLint candidate_dither = get_uniform_location(candidate, "ditherAmount");
    if (candidate_source < 0 || candidate_inverse_size < 0 || candidate_deband < 0 ||
        candidate_dither < 0) {
        log_line("shader link omitted one or more required compositor uniforms");
        delete_program(candidate);
        return false;
    }
    if (compositor_program) delete_program(compositor_program);
    compositor_program = candidate;
    source_uniform = candidate_source;
    inverse_size_uniform = candidate_inverse_size;
    deband_uniform = candidate_deband;
    dither_uniform = candidate_dither;
    log_line("compositor shader ready vertex=%s fragment=%s",
             vertex_override.empty() ? "embedded" : "override",
             fragment_override.empty() ? "embedded" : "override");
    return true;
}

GLuint create_linked_program(const char* label, const char* vertex_source,
                             const char* fragment_source) {
    const GLuint vertex = compile_stage(GL_VERTEX_SHADER, vertex_source);
    const GLuint fragment = compile_stage(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex || !fragment) {
        if (vertex) delete_shader(vertex);
        if (fragment) delete_shader(fragment);
        log_line("%s shader compilation failed", label);
        return 0;
    }
    const GLuint program = create_program();
    attach_shader(program, vertex);
    attach_shader(program, fragment);
    link_program(program);
    delete_shader(vertex);
    delete_shader(fragment);
    GLint linked = 0;
    get_program_iv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char info[2048]{};
        GLsizei length = 0;
        get_program_info_log(program, sizeof(info), &length, info);
        log_line("%s shader link failed: %s", label, info);
        delete_program(program);
        return 0;
    }
    return program;
}

bool create_crt_programs() {
    if (!g_config.crt_enabled) {
        crt_ready = false;
        return true;
    }
    static constexpr char vertex_source[] = R"GLSL(
#version 120
varying vec2 textureCoordinate;
void main() {
    gl_Position = gl_Vertex;
    textureCoordinate = gl_MultiTexCoord0.xy;
}
)GLSL";
    static constexpr char signal_fragment[] = R"GLSL(
#version 120
uniform sampler2D sourceTexture;
uniform vec2 inverseSize;
uniform float debandAmount;
varying vec2 textureCoordinate;

float toLinear(float encoded) {
    return encoded <= 0.04045 ? encoded / 12.92
                              : pow((encoded + 0.055) / 1.055, 2.4);
}

void main() {
    vec3 center = texture2D(sourceTexture, textureCoordinate).rgb;
    vec3 leftColor = texture2D(sourceTexture, textureCoordinate - vec2(inverseSize.x, 0.0)).rgb;
    vec3 rightColor = texture2D(sourceTexture, textureCoordinate + vec2(inverseSize.x, 0.0)).rgb;
    vec3 downColor = texture2D(sourceTexture, textureCoordinate - vec2(0.0, inverseSize.y)).rgb;
    vec3 upColor = texture2D(sourceTexture, textureCoordinate + vec2(0.0, inverseSize.y)).rgb;
    vec3 averageColor = (leftColor + rightColor + downColor + upColor) * 0.25;
    vec3 localRange = max(max(abs(center - leftColor), abs(center - rightColor)),
                          max(abs(center - downColor), abs(center - upColor)));
    float difference = max(max(localRange.r, localRange.g), localRange.b);
    float weight = 1.0 - smoothstep(1.0 / 255.0, 4.0 / 255.0, difference);
    center = clamp(mix(center, averageColor, weight * 0.18 * debandAmount), 0.0, 1.0);
    gl_FragColor = vec4(toLinear(center.r), toLinear(center.g), toLinear(center.b), 1.0);
}
)GLSL";
    static constexpr char response_fragment[] = R"GLSL(
#version 120
uniform sampler2D sourceTexture;
uniform vec2 signalSize;
uniform float maskStrength;
uniform float scanlineStrength;
varying vec2 textureCoordinate;

void main() {
    vec3 color = texture2D(sourceTexture, textureCoordinate).rgb;
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float rowPhase = fract(textureCoordinate.y * signalSize.y);
    float trough = 0.5 - 0.5 * cos(rowPhase * 6.28318530718);
    float beamLoss = mix(0.18, 0.09, clamp(sqrt(max(luma, 0.0)), 0.0, 1.0));
    color *= 1.0 - scanlineStrength * beamLoss * trough;

    float phase = mod(floor(gl_FragCoord.x), 3.0);
    vec3 mask = vec3(1.0 - maskStrength);
    if (phase < 0.5) mask.r = 1.0 + 2.0 * maskStrength;
    else if (phase < 1.5) mask.g = 1.0 + 2.0 * maskStrength;
    else mask.b = 1.0 + 2.0 * maskStrength;
    gl_FragColor = vec4(max(color * mask, 0.0), 1.0);
}
)GLSL";
    static constexpr char blur_fragment[] = R"GLSL(
#version 120
uniform sampler2D sourceTexture;
uniform vec2 inverseSize;
uniform vec2 blurDirection;
uniform float extractHighlights;
varying vec2 textureCoordinate;

vec3 sourceSample(vec2 coordinate) {
    vec3 color = texture2D(sourceTexture, coordinate).rgb;
    if (extractHighlights > 0.5) {
        float peak = max(max(color.r, color.g), color.b);
        float weight = smoothstep(0.25, 0.85, peak);
        color *= weight;
    }
    return color;
}

void main() {
    vec2 stepVector = inverseSize * blurDirection;
    vec3 color = sourceSample(textureCoordinate) * 0.2270270270;
    color += sourceSample(textureCoordinate + stepVector * 1.3846153846) * 0.3162162162;
    color += sourceSample(textureCoordinate - stepVector * 1.3846153846) * 0.3162162162;
    color += sourceSample(textureCoordinate + stepVector * 3.2307692308) * 0.0702702703;
    color += sourceSample(textureCoordinate - stepVector * 3.2307692308) * 0.0702702703;
    gl_FragColor = vec4(color, 1.0);
}
)GLSL";
    static constexpr char present_fragment[] = R"GLSL(
#version 120
uniform sampler2D sourceTexture;
uniform sampler2D blurTexture;
uniform float bloomStrength;
uniform float halationStrength;
uniform float ditherAmount;
varying vec2 textureCoordinate;

float toSrgb(float linearValue) {
    linearValue = max(linearValue, 0.0);
    return linearValue <= 0.0031308 ? linearValue * 12.92
                                   : 1.055 * pow(linearValue, 1.0 / 2.4) - 0.055;
}

float hashNoise(vec2 position) {
    return fract(52.9829189 * fract(dot(position, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec3 response = texture2D(sourceTexture, textureCoordinate).rgb;
    vec3 blurred = texture2D(blurTexture, textureCoordinate).rgb;
    vec3 halo = max(blurred - response * 0.35, 0.0);
    vec3 linearColor = response + blurred * bloomStrength + halo * halationStrength;
    vec3 encoded = vec3(toSrgb(linearColor.r), toSrgb(linearColor.g), toSrgb(linearColor.b));
    float noise = hashNoise(gl_FragCoord.xy) - 0.5;
    encoded += noise * (0.5 / 255.0) * ditherAmount;
    gl_FragColor = vec4(clamp(encoded, 0.0, 1.0), 1.0);
}
)GLSL";

    std::string vertex_override;
    std::string signal_override;
    std::string response_override;
    std::string blur_override;
    std::string present_override;
    const char* selected_vertex = vertex_source;
    const char* selected_signal = signal_fragment;
    const char* selected_response = response_fragment;
    const char* selected_blur = blur_fragment;
    const char* selected_present = present_fragment;
    if (g_config.development_hot_reload) {
        vertex_override = read_shader_override("compositor.vert");
        signal_override = read_shader_override("crt_signal.frag");
        response_override = read_shader_override("crt_response.frag");
        blur_override = read_shader_override("crt_blur.frag");
        present_override = read_shader_override("crt_present.frag");
        if (!vertex_override.empty()) selected_vertex = vertex_override.c_str();
        if (!signal_override.empty()) selected_signal = signal_override.c_str();
        if (!response_override.empty()) selected_response = response_override.c_str();
        if (!blur_override.empty()) selected_blur = blur_override.c_str();
        if (!present_override.empty()) selected_present = present_override.c_str();
    }

    const GLuint signal = create_linked_program("CRT signal", selected_vertex, selected_signal);
    const GLuint response = create_linked_program("CRT response", selected_vertex, selected_response);
    const GLuint blur = create_linked_program("CRT blur", selected_vertex, selected_blur);
    const GLuint present = create_linked_program("CRT present", selected_vertex, selected_present);
    if (!signal || !response || !blur || !present) {
        if (signal) delete_program(signal);
        if (response) delete_program(response);
        if (blur) delete_program(blur);
        if (present) delete_program(present);
        return false;
    }

    const GLint signal_source = get_uniform_location(signal, "sourceTexture");
    const GLint signal_inverse = get_uniform_location(signal, "inverseSize");
    const GLint signal_deband = get_uniform_location(signal, "debandAmount");
    const GLint response_source = get_uniform_location(response, "sourceTexture");
    const GLint response_size = get_uniform_location(response, "signalSize");
    const GLint response_mask = get_uniform_location(response, "maskStrength");
    const GLint response_scanline = get_uniform_location(response, "scanlineStrength");
    const GLint blur_source = get_uniform_location(blur, "sourceTexture");
    const GLint blur_inverse = get_uniform_location(blur, "inverseSize");
    const GLint blur_direction = get_uniform_location(blur, "blurDirection");
    const GLint blur_extract = get_uniform_location(blur, "extractHighlights");
    const GLint present_source = get_uniform_location(present, "sourceTexture");
    const GLint present_blur = get_uniform_location(present, "blurTexture");
    const GLint present_bloom = get_uniform_location(present, "bloomStrength");
    const GLint present_halation = get_uniform_location(present, "halationStrength");
    const GLint present_dither = get_uniform_location(present, "ditherAmount");
    if (signal_source < 0 || signal_inverse < 0 || signal_deband < 0 ||
        response_source < 0 || response_size < 0 || response_mask < 0 ||
        response_scanline < 0 || blur_source < 0 || blur_inverse < 0 ||
        blur_direction < 0 || blur_extract < 0 || present_source < 0 ||
        present_blur < 0 || present_bloom < 0 || present_halation < 0 ||
        present_dither < 0) {
        log_line("CRT shader link omitted one or more required uniforms");
        delete_program(signal);
        delete_program(response);
        delete_program(blur);
        delete_program(present);
        return false;
    }

    if (crt_signal_program) delete_program(crt_signal_program);
    if (crt_response_program) delete_program(crt_response_program);
    if (crt_blur_program) delete_program(crt_blur_program);
    if (crt_present_program) delete_program(crt_present_program);
    crt_signal_program = signal;
    crt_response_program = response;
    crt_blur_program = blur;
    crt_present_program = present;
    crt_signal_source_uniform = signal_source;
    crt_signal_inverse_size_uniform = signal_inverse;
    crt_signal_deband_uniform = signal_deband;
    crt_response_source_uniform = response_source;
    crt_response_signal_size_uniform = response_size;
    crt_response_mask_uniform = response_mask;
    crt_response_scanline_uniform = response_scanline;
    crt_blur_source_uniform = blur_source;
    crt_blur_inverse_size_uniform = blur_inverse;
    crt_blur_direction_uniform = blur_direction;
    crt_blur_extract_uniform = blur_extract;
    crt_present_source_uniform = present_source;
    crt_present_blur_uniform = present_blur;
    crt_present_bloom_uniform = present_bloom;
    crt_present_halation_uniform = present_halation;
    crt_present_dither_uniform = present_dither;
    log_line("CRT shaders ready signal=%s response=%s blur=%s present=%s",
             signal_override.empty() ? "embedded" : "override",
             response_override.empty() ? "embedded" : "override",
             blur_override.empty() ? "embedded" : "override",
             present_override.empty() ? "embedded" : "override");
    return true;
}

bool capture_frame(const char* label, GLuint framebuffer, GLenum buffer, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
    if (pixel_count > 268435456) return false;
    std::vector<unsigned char> pixels(pixel_count * 4);
    bind_framebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glReadBuffer(buffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (glGetError() != GL_NO_ERROR) {
        log_line("capture readback failed label=%s", label);
        return false;
    }
    for (std::size_t index = 0; index < pixels.size(); index += 4)
        std::swap(pixels[index], pixels[index + 2]);

    SYSTEMTIME now{};
    GetLocalTime(&now);
    char path[MAX_PATH]{};
    std::snprintf(path, MAX_PATH,
                  "%s\\aitd4-renderer-%s-%04u%02u%02u-%02u%02u%02u-%03u.tga",
                  g_module_directory, label, now.wYear, now.wMonth, now.wDay,
                  now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    unsigned char header[18]{};
    header[2] = 2;
    header[12] = static_cast<unsigned char>(width & 0xff);
    header[13] = static_cast<unsigned char>((width >> 8) & 0xff);
    header[14] = static_cast<unsigned char>(height & 0xff);
    header[15] = static_cast<unsigned char>((height >> 8) & 0xff);
    header[16] = 32;
    header[17] = 8;
    FILE* file = nullptr;
    if (fopen_s(&file, path, "wb") != 0 || !file) return false;
    const bool written = std::fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
                         std::fwrite(pixels.data(), 1, pixels.size(), file) == pixels.size();
    std::fclose(file);
    log_line("capture %s label=%s size=%dx%d path=%s", written ? "saved" : "failed",
             label, width, height, path);
    return written;
}

bool create_color_target(int width, int height, GLenum internal_format,
                         GLuint& framebuffer, GLuint& texture) {
    gen_framebuffers(1, &framebuffer);
    bind_framebuffer(GL_FRAMEBUFFER, framebuffer);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, GL_RGBA,
                 internal_format == GL_RGBA16F ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    return check_framebuffer_status(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

void draw_fullscreen_quad() {
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, 1.0f);
    glEnd();
}

bool present_crt_texture(GLuint input_texture, int input_width, int input_height,
                         bool apply_deband) {
    if (!crt_ready || !input_texture || input_width <= 0 || input_height <= 0) return false;
    while (glGetError() != GL_NO_ERROR) {}
    const int signal_width = g_config.crt_signal_width;
    const int signal_height = g_config.crt_signal_height;
    const int output_width = output_viewport.width;
    const int output_height = output_viewport.height;
    const int blur_width = std::max(1, output_width / 2);
    const int blur_height = std::max(1, output_height / 2);

    active_texture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, input_texture);
    bind_framebuffer(GL_FRAMEBUFFER, crt_signal_fbo);
    glViewport(0, 0, signal_width, signal_height);
    use_program(crt_signal_program);
    uniform_1i(crt_signal_source_uniform, 0);
    uniform_2f(crt_signal_inverse_size_uniform, 1.0f / input_width, 1.0f / input_height);
    uniform_1f(crt_signal_deband_uniform, apply_deband ? 1.0f : 0.0f);
    draw_fullscreen_quad();

    glBindTexture(GL_TEXTURE_2D, crt_signal_texture);
    bind_framebuffer(GL_FRAMEBUFFER, crt_response_fbo);
    glViewport(0, 0, output_width, output_height);
    use_program(crt_response_program);
    uniform_1i(crt_response_source_uniform, 0);
    uniform_2f(crt_response_signal_size_uniform, static_cast<float>(signal_width),
               static_cast<float>(signal_height));
    uniform_1f(crt_response_mask_uniform, g_config.crt_mask_strength);
    uniform_1f(crt_response_scanline_uniform, g_config.crt_scanline_strength);
    draw_fullscreen_quad();

    glBindTexture(GL_TEXTURE_2D, crt_response_texture);
    bind_framebuffer(GL_FRAMEBUFFER, crt_blur_horizontal_fbo);
    glViewport(0, 0, blur_width, blur_height);
    use_program(crt_blur_program);
    uniform_1i(crt_blur_source_uniform, 0);
    uniform_2f(crt_blur_inverse_size_uniform, 1.0f / output_width, 1.0f / output_height);
    uniform_2f(crt_blur_direction_uniform, 1.0f, 0.0f);
    uniform_1f(crt_blur_extract_uniform, 1.0f);
    draw_fullscreen_quad();

    glBindTexture(GL_TEXTURE_2D, crt_blur_horizontal_texture);
    bind_framebuffer(GL_FRAMEBUFFER, crt_blur_vertical_fbo);
    glViewport(0, 0, blur_width, blur_height);
    uniform_2f(crt_blur_inverse_size_uniform, 1.0f / blur_width, 1.0f / blur_height);
    uniform_2f(crt_blur_direction_uniform, 0.0f, 1.0f);
    uniform_1f(crt_blur_extract_uniform, 0.0f);
    draw_fullscreen_quad();

    bind_framebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, monitor_rect.right - monitor_rect.left,
               monitor_rect.bottom - monitor_rect.top);
    glDrawBuffer(GL_BACK);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(output_viewport.x, output_viewport.y, output_width, output_height);
    active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, crt_response_texture);
    active_texture(GL_TEXTURE0 + 1);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, crt_blur_vertical_texture);
    use_program(crt_present_program);
    uniform_1i(crt_present_source_uniform, 0);
    uniform_1i(crt_present_blur_uniform, 1);
    uniform_1f(crt_present_bloom_uniform, g_config.crt_bloom_strength);
    uniform_1f(crt_present_halation_uniform, g_config.crt_halation_strength);
    uniform_1f(crt_present_dither_uniform, g_config.dither ? 1.0f : 0.0f);
    draw_fullscreen_quad();
    active_texture(GL_TEXTURE0);
    return glGetError() == GL_NO_ERROR;
}

bool initialize_framebuffer() {
    if (framebuffer_ready) return true;
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (!version || sscanf_s(version, "%d.%d", &gl_major, &gl_minor) != 2 ||
        gl_major < 3 || (gl_major == 3 && gl_minor < 3)) {
        log_line("OpenGL 3.3 compatibility unavailable version=%s", version ? version : "null");
        return false;
    }
    bool loaded =
        load_gl_extension(gen_framebuffers, "glGenFramebuffers") &&
        load_gl_extension(delete_framebuffers, "glDeleteFramebuffers") &&
        load_gl_extension(bind_framebuffer, "glBindFramebuffer") &&
        load_gl_extension(check_framebuffer_status, "glCheckFramebufferStatus") &&
        load_gl_extension(framebuffer_texture_2d, "glFramebufferTexture2D") &&
        load_gl_extension(blit_framebuffer, "glBlitFramebuffer") &&
        load_gl_extension(gen_renderbuffers, "glGenRenderbuffers") &&
        load_gl_extension(delete_renderbuffers, "glDeleteRenderbuffers") &&
        load_gl_extension(bind_renderbuffer, "glBindRenderbuffer") &&
        load_gl_extension(renderbuffer_storage_multisample, "glRenderbufferStorageMultisample") &&
        load_gl_extension(framebuffer_renderbuffer, "glFramebufferRenderbuffer") &&
        load_gl_extension(create_shader, "glCreateShader") &&
        load_gl_extension(shader_source, "glShaderSource") &&
        load_gl_extension(compile_shader, "glCompileShader") &&
        load_gl_extension(get_shader_iv, "glGetShaderiv") &&
        load_gl_extension(get_shader_info_log, "glGetShaderInfoLog") &&
        load_gl_extension(create_program, "glCreateProgram") &&
        load_gl_extension(attach_shader, "glAttachShader") &&
        load_gl_extension(link_program, "glLinkProgram") &&
        load_gl_extension(get_program_iv, "glGetProgramiv") &&
        load_gl_extension(get_program_info_log, "glGetProgramInfoLog") &&
        load_gl_extension(delete_shader, "glDeleteShader") &&
        load_gl_extension(delete_program, "glDeleteProgram") &&
        load_gl_extension(use_program, "glUseProgram") &&
        load_gl_extension(get_uniform_location, "glGetUniformLocation") &&
        load_gl_extension(uniform_1i, "glUniform1i") &&
        load_gl_extension(uniform_1f, "glUniform1f") &&
        load_gl_extension(uniform_2f, "glUniform2f") &&
        load_gl_extension(active_texture, "glActiveTexture");
    if (!loaded) return false;
    load_gl_extension(swap_interval, "wglSwapIntervalEXT");
    if (swap_interval) swap_interval(g_config.vsync ? 1 : 0);

    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (extensions && std::strstr(extensions, "GL_EXT_texture_filter_anisotropic")) {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_anisotropy);
        if (!std::isfinite(max_anisotropy) || max_anisotropy < 1.0f) max_anisotropy = 1.0f;
    }
    GLint maximum_samples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maximum_samples);
    maximum_samples = std::max(1, maximum_samples);
    actual_samples = std::clamp(std::max(1, g_config.msaa), 1, maximum_samples);
    const int width = render_width;
    const int height = render_height;

    inside_compositor = true;
    gen_framebuffers(1, &multisample_fbo);
    bind_framebuffer(GL_FRAMEBUFFER, multisample_fbo);
    gen_renderbuffers(1, &multisample_color);
    bind_renderbuffer(GL_RENDERBUFFER, multisample_color);
    renderbuffer_storage_multisample(GL_RENDERBUFFER, actual_samples, GL_RGBA8, width, height);
    framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                             multisample_color);
    gen_renderbuffers(1, &multisample_depth_stencil);
    bind_renderbuffer(GL_RENDERBUFFER, multisample_depth_stencil);
    renderbuffer_storage_multisample(GL_RENDERBUFFER, actual_samples, GL_DEPTH24_STENCIL8,
                                     width, height);
    framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                             multisample_depth_stencil);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    if (check_framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        inside_compositor = false;
        return false;
    }

    gen_framebuffers(1, &resolve_fbo);
    bind_framebuffer(GL_FRAMEBUFFER, resolve_fbo);
    glGenTextures(1, &resolve_texture);
    glBindTexture(GL_TEXTURE_2D, resolve_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           resolve_texture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    if (check_framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        !create_compositor_program() || !create_crt_programs()) {
        inside_compositor = false;
        return false;
    }
    if (g_config.crt_enabled) {
        const int blur_width = std::max(1, output_viewport.width / 2);
        const int blur_height = std::max(1, output_viewport.height / 2);
        if (!create_color_target(g_config.crt_signal_width, g_config.crt_signal_height,
                                 GL_RGBA16F, crt_signal_fbo, crt_signal_texture) ||
            !create_color_target(output_viewport.width, output_viewport.height,
                                 GL_RGBA16F, crt_response_fbo, crt_response_texture) ||
            !create_color_target(blur_width, blur_height, GL_RGBA16F,
                                 crt_blur_horizontal_fbo, crt_blur_horizontal_texture) ||
            !create_color_target(blur_width, blur_height, GL_RGBA16F,
                                 crt_blur_vertical_fbo, crt_blur_vertical_texture)) {
            log_line("CRT framebuffer creation failed signal=%dx%d response=%dx%d blur=%dx%d",
                     g_config.crt_signal_width, g_config.crt_signal_height,
                     output_viewport.width, output_viewport.height, blur_width, blur_height);
            inside_compositor = false;
            return false;
        }
        glGenTextures(1, &bink_canvas_texture);
        glBindTexture(GL_TEXTURE_2D, bink_canvas_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 640, 480, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        if (glGetError() != GL_NO_ERROR) {
            log_line("CRT Bink canvas texture creation failed");
            inside_compositor = false;
            return false;
        }
        crt_ready = true;
    }
    bind_framebuffer(GL_FRAMEBUFFER, multisample_fbo);
    // This game does not reliably call glViewport after creating its context. The OpenGL
    // default therefore reflects the physical borderless window (for example 1920x1080),
    // not the logical 4:3 framebuffer. Initialize both rectangles explicitly so the first
    // title/movie frame cannot be clipped or appear zoomed.
    glViewport(0, 0, width, height);
    glScissor(0, 0, width, height);
    source_width = width;
    source_height = height;
    inside_compositor = false;
    framebuffer_ready = true;
    game_context = wglGetCurrentContext();
    log_line("OpenGL compositor ready version=%s logical=%dx%d physical=%dx%d samples=%d aniso=%.1f crt=%d signal=%dx%d",
             version, width, height, monitor_rect.right - monitor_rect.left,
             monitor_rect.bottom - monitor_rect.top, actual_samples, max_anisotropy,
             crt_ready ? 1 : 0, g_config.crt_signal_width, g_config.crt_signal_height);
    return true;
}

void APIENTRY hooked_gl_clear(GLbitfield mask) {
    if (!framebuffer_ready && !initialize_framebuffer())
        fatal_graphics("OpenGL 3.3 framebuffer initialization failed. The game will not run partially patched.");
    if (!inside_compositor) bind_framebuffer(GL_FRAMEBUFFER, multisample_fbo);
    real_gl_clear(mask);
}

void APIENTRY hooked_gl_viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (!inside_compositor) {
        const GLint original_x = x;
        const GLint original_y = y;
        const GLsizei original_width = width;
        const GLsizei original_height = height;
        const bool full_frame = x == 0 && y == 0 && width >= 640 && height >= 480 &&
                                static_cast<long long>(width) * 3 ==
                                    static_cast<long long>(height) * 4;
        const bool physical_frame = x == 0 && y == 0 &&
            width == monitor_rect.right - monitor_rect.left &&
            height == monitor_rect.bottom - monitor_rect.top;
        if (full_frame || physical_frame) {
            if (full_frame) {
                source_width = width;
                source_height = height;
            }
            width = render_width;
            height = render_height;
        } else {
            scale_legacy_rectangle(x, y, width, height);
        }
        if (viewport_log_count < 32) {
            log_line("viewport source=%d,%d %dx%d mapped=%d,%d %dx%d source_canvas=%dx%d",
                     original_x, original_y, original_width, original_height,
                     x, y, width, height, source_width, source_height);
            ++viewport_log_count;
        }
    }
    real_gl_viewport(x, y, width, height);
}

void APIENTRY hooked_gl_scissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (!inside_compositor) {
        const GLint original_x = x;
        const GLint original_y = y;
        const GLsizei original_width = width;
        const GLsizei original_height = height;
        const bool full_frame = x == 0 && y == 0 && width >= 640 && height >= 480 &&
                                static_cast<long long>(width) * 3 ==
                                    static_cast<long long>(height) * 4;
        if (full_frame) {
            source_width = width;
            source_height = height;
            width = render_width;
            height = render_height;
        } else {
            scale_legacy_rectangle(x, y, width, height);
        }
        if (scissor_log_count < 32) {
            log_line("scissor source=%d,%d %dx%d mapped=%d,%d %dx%d source_canvas=%dx%d",
                     original_x, original_y, original_width, original_height,
                     x, y, width, height, source_width, source_height);
            ++scissor_log_count;
        }
    }
    real_gl_scissor(x, y, width, height);
}

GLint corrected_texture_parameter(GLenum pname, GLint value) {
    if (!inside_compositor && g_config.fix_mask_seams &&
        (pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T) && value == GL_CLAMP)
        return GL_CLAMP_TO_EDGE;
    return value;
}

void apply_anisotropy_policy(GLenum target, GLenum pname, GLint value) {
    const bool mipmapped_3d_filter = pname == GL_TEXTURE_MIN_FILTER &&
        (value == GL_NEAREST_MIPMAP_NEAREST || value == GL_LINEAR_MIPMAP_NEAREST ||
         value == GL_NEAREST_MIPMAP_LINEAR || value == GL_LINEAR_MIPMAP_LINEAR);
    if (!inside_compositor && target == GL_TEXTURE_2D && pname == GL_TEXTURE_MIN_FILTER &&
        max_anisotropy > 1.0f) {
        const float requested = mipmapped_3d_filter
            ? std::min(static_cast<float>(g_config.anisotropy), max_anisotropy)
            : 1.0f;
        glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, requested);
    }
}

void APIENTRY hooked_gl_tex_parameter_i(GLenum target, GLenum pname, GLint value) {
    value = corrected_texture_parameter(pname, value);
    real_gl_tex_parameter_i(target, pname, value);
    apply_anisotropy_policy(target, pname, value);
}

void APIENTRY hooked_gl_tex_parameter_f(GLenum target, GLenum pname, GLfloat value) {
    const GLint integral = corrected_texture_parameter(pname, static_cast<GLint>(value));
    if (integral != static_cast<GLint>(value)) value = static_cast<GLfloat>(integral);
    real_gl_tex_parameter_f(target, pname, value);
    apply_anisotropy_policy(target, pname, static_cast<GLint>(value));
}

void APIENTRY hooked_gl_tex_parameter_fv(GLenum target, GLenum pname, const GLfloat* values) {
    const bool scalar_policy = pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_WRAP_S ||
                               pname == GL_TEXTURE_WRAP_T;
    if (!values || !scalar_policy) {
        real_gl_tex_parameter_fv(target, pname, values);
        return;
    }
    GLfloat corrected = values[0];
    const GLint integral = corrected_texture_parameter(pname, static_cast<GLint>(corrected));
    if (integral != static_cast<GLint>(corrected)) corrected = static_cast<GLfloat>(integral);
    real_gl_tex_parameter_fv(target, pname, &corrected);
    apply_anisotropy_policy(target, pname, static_cast<GLint>(corrected));
}

void APIENTRY hooked_gl_tex_parameter_iv(GLenum target, GLenum pname, const GLint* values) {
    const bool scalar_policy = pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_WRAP_S ||
                               pname == GL_TEXTURE_WRAP_T;
    if (!values || !scalar_policy) {
        real_gl_tex_parameter_iv(target, pname, values);
        return;
    }
    const GLint corrected = corrected_texture_parameter(pname, values[0]);
    real_gl_tex_parameter_iv(target, pname, &corrected);
    apply_anisotropy_policy(target, pname, corrected);
}

void APIENTRY hooked_gl_draw_buffer(GLenum buffer) {
    if (!inside_compositor && framebuffer_ready && (buffer == GL_BACK || buffer == GL_FRONT))
        buffer = GL_COLOR_ATTACHMENT0;
    real_gl_draw_buffer(buffer);
}

void APIENTRY hooked_gl_read_buffer(GLenum buffer) {
    if (!inside_compositor && framebuffer_ready && (buffer == GL_BACK || buffer == GL_FRONT))
        buffer = GL_COLOR_ATTACHMENT0;
    real_gl_read_buffer(buffer);
}

bool should_log_pixel_call() {
    LONG remaining = InterlockedCompareExchange(&fmv_pixel_trace_budget, 0, 0);
    while (remaining > 0) {
        if (InterlockedCompareExchange(&fmv_pixel_trace_budget, remaining - 1, remaining) == remaining)
            return true;
        remaining = InterlockedCompareExchange(&fmv_pixel_trace_budget, 0, 0);
    }
    if (startup_pixel_log_count >= 64) return false;
    ++startup_pixel_log_count;
    return true;
}

void APIENTRY hooked_gl_pixel_zoom(GLfloat xfactor, GLfloat yfactor) {
    if (should_log_pixel_call()) {
        log_line("glPixelZoom x=%.6f y=%.6f", xfactor, yfactor);
    }
    real_gl_pixel_zoom(xfactor, yfactor);
}

void APIENTRY hooked_gl_draw_pixels(GLsizei width, GLsizei height, GLenum format, GLenum type,
                                    const void* pixels) {
    if (should_log_pixel_call()) {
        log_line("glDrawPixels size=%dx%d format=%04X type=%04X pixels=%p",
                 width, height, format, type, pixels);
    }
    real_gl_draw_pixels(width, height, format, type, pixels);
}

void APIENTRY hooked_gl_tex_image_2d(GLenum target, GLint level, GLint internal_format,
                                     GLsizei width, GLsizei height, GLint border, GLenum format,
                                     GLenum type, const void* pixels) {
    if (should_log_pixel_call()) {
        log_line("glTexImage2D target=%04X level=%d internal=%d size=%dx%d format=%04X type=%04X pixels=%p",
                 target, level, internal_format, width, height, format, type, pixels);
    }
    real_gl_tex_image_2d(target, level, internal_format, width, height, border, format, type, pixels);
}

void APIENTRY hooked_gl_tex_sub_image_2d(GLenum target, GLint level, GLint x, GLint y,
                                         GLsizei width, GLsizei height, GLenum format, GLenum type,
                                         const void* pixels) {
    if (should_log_pixel_call()) {
        log_line("glTexSubImage2D target=%04X level=%d offset=%d,%d size=%dx%d format=%04X type=%04X pixels=%p",
                 target, level, x, y, width, height, format, type, pixels);
    }
    real_gl_tex_sub_image_2d(target, level, x, y, width, height, format, type, pixels);
}

BOOL WINAPI hooked_swap_buffers(HDC dc) {
    if (!framebuffer_ready) return real_swap_buffers(dc);
    if (g_config.development_hot_reload && (GetAsyncKeyState(VK_F11) & 1)) {
        if (!reload_runtime_config()) {
            log_line("hot reload rejected; configuration is invalid");
        } else {
            if (swap_interval) swap_interval(g_config.vsync ? 1 : 0);
            if (!create_compositor_program() || !create_crt_programs())
                log_line("hot reload rejected; one or more previous programs retained");
        }
    }
    DWORD capture_requests = static_cast<DWORD>(InterlockedExchange(&capture_request_flags, 0));
    if (g_config.development_capture && (GetAsyncKeyState(VK_F9) & 1))
        capture_requests |= renderer_capture_raw;
    if (g_config.development_capture && (GetAsyncKeyState(VK_F10) & 1))
        capture_requests |= renderer_capture_output;
    const bool capture_raw = (capture_requests & renderer_capture_raw) != 0;
    const bool capture_output = (capture_requests & renderer_capture_output) != 0;
    inside_compositor = true;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    GLint previous_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);

    bind_framebuffer(GL_READ_FRAMEBUFFER, multisample_fbo);
    bind_framebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo);
    blit_framebuffer(0, 0, render_width, render_height,
                     0, 0, render_width, render_height,
                     GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (capture_raw)
        capture_frame("raw", resolve_fbo, GL_COLOR_ATTACHMENT0, render_width, render_height);
    bind_framebuffer(GL_FRAMEBUFFER, 0);

    glViewport(0, 0, monitor_rect.right - monitor_rect.left,
               monitor_rect.bottom - monitor_rect.top);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_POLYGON_STIPPLE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_POLYGON_OFFSET_LINE);
    glDisable(GL_POLYGON_OFFSET_POINT);
    glDisable(GL_FOG);
    glDisable(GL_LIGHTING);
    glDisable(GL_SCISSOR_TEST);
    if (!framebuffer_srgb_logged) {
        log_line("default framebuffer sRGB state before compositor=%d",
                 glIsEnabled(GL_FRAMEBUFFER_SRGB) ? 1 : 0);
        framebuffer_srgb_logged = true;
    }
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_CLIP_PLANE0);
    glDisable(GL_CLIP_PLANE1);
    glDisable(GL_CLIP_PLANE2);
    glDisable(GL_CLIP_PLANE3);
    glDisable(GL_CLIP_PLANE4);
    glDisable(GL_CLIP_PLANE5);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDrawBuffer(GL_BACK);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    if (g_config.crt_enabled) {
        if (!present_crt_texture(resolve_texture, render_width, render_height, g_config.deband))
            fatal_graphics("CRT gameplay composition failed. The game will not continue with a partial renderer.");
    } else {
        active_texture(GL_TEXTURE0);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, resolve_texture);
        use_program(compositor_program);
        uniform_1i(source_uniform, 0);
        uniform_2f(inverse_size_uniform, 1.0f / render_width,
                   1.0f / render_height);
        uniform_1f(deband_uniform, g_config.deband ? 1.0f : 0.0f);
        uniform_1f(dither_uniform, g_config.dither ? 1.0f : 0.0f);

        const float physical_width = static_cast<float>(monitor_rect.right - monitor_rect.left);
        const float physical_height = static_cast<float>(monitor_rect.bottom - monitor_rect.top);
        const float left = output_viewport.x / physical_width * 2.0f - 1.0f;
        const float right = (output_viewport.x + output_viewport.width) / physical_width * 2.0f - 1.0f;
        const float bottom = output_viewport.y / physical_height * 2.0f - 1.0f;
        const float top = (output_viewport.y + output_viewport.height) / physical_height * 2.0f - 1.0f;
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(left, bottom);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(right, bottom);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(right, top);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(left, top);
        glEnd();
    }

    if (capture_output)
        capture_frame("output", 0, GL_BACK, monitor_rect.right - monitor_rect.left,
                      monitor_rect.bottom - monitor_rect.top);

    use_program(static_cast<GLuint>(previous_program));
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
    const BOOL result = real_swap_buffers(dc);
    if (alternate_bink_frame_pending) {
        if (!result)
            fatal_graphics("The native OpenGL Bink backend failed to present its CRT-composited frame.");
        alternate_bink_frame_pending = false;
        ++bink_presented_frames;
        ++active_bink_presented_frames;
        if (active_bink_presented_frames <= 8 || active_bink_presented_frames % 120 == 0)
            log_line("native OpenGL Bink frame CRT-presented movie=%p presented=%u format=%u pitch=%d",
                     active_bink_movie, active_bink_presented_frames,
                     last_bink_copy_flags & bink_surface_mask, last_bink_pitch);
    }
    bind_framebuffer(GL_FRAMEBUFFER, multisample_fbo);
    inside_compositor = false;
    return result;
}

FARPROC WINAPI hooked_get_proc_address(HMODULE module, LPCSTR name) {
    FARPROC original = real_get_proc_address(module, name);
    if (!name || reinterpret_cast<ULONG_PTR>(name) <= 0xFFFF) return original;
    if (!is_graphics_module(module)) return original;
    if (std::strncmp(name, "wgl", 3) == 0 || std::strcmp(name, "SwapBuffers") == 0)
        log_line("dynamic graphics lookup name=%s original=%p", name, original);
    if (std::strcmp(name, "wglCreateContext") == 0) {
        if (!original) return nullptr;
        real_wgl_create_context = reinterpret_cast<WglCreateContextFn>(original);
        return reinterpret_cast<FARPROC>(hooked_wgl_create_context);
    }
    if (std::strcmp(name, "wglDeleteContext") == 0) {
        if (!original) return nullptr;
        real_wgl_delete_context = reinterpret_cast<WglDeleteContextFn>(original);
        return reinterpret_cast<FARPROC>(hooked_wgl_delete_context);
    }
    if (std::strcmp(name, "wglMakeCurrent") == 0) {
        if (!original) return nullptr;
        real_wgl_make_current = reinterpret_cast<WglMakeCurrentFn>(original);
        return reinterpret_cast<FARPROC>(hooked_wgl_make_current);
    }
    if (std::strcmp(name, "ChoosePixelFormat") == 0 ||
        std::strcmp(name, "wglChoosePixelFormat") == 0) {
        if (!original)
            original = ::GetProcAddress(GetModuleHandleA("gdi32.dll"), "ChoosePixelFormat");
        real_choose_pixel_format = reinterpret_cast<ChoosePixelFormatFn>(original);
        return reinterpret_cast<FARPROC>(hooked_choose_pixel_format);
    }
    if (std::strcmp(name, "glClear") == 0) {
        if (!original) return nullptr;
        real_gl_clear = reinterpret_cast<GlClearFn>(original);
        return reinterpret_cast<FARPROC>(hooked_gl_clear);
    }
    if (std::strcmp(name, "glViewport") == 0) {
        if (!original) return nullptr;
        real_gl_viewport = reinterpret_cast<GlViewportFn>(original);
        return reinterpret_cast<FARPROC>(hooked_gl_viewport);
    }
    if (std::strcmp(name, "glScissor") == 0) {
        if (!original) return nullptr;
        real_gl_scissor = reinterpret_cast<GlScissorFn>(original);
        return reinterpret_cast<FARPROC>(hooked_gl_scissor);
    }
    if (std::strcmp(name, "glTexParameteri") == 0) {
        if (!original) return nullptr;
        real_gl_tex_parameter_i = reinterpret_cast<GlTexParameteriFn>(original);
        log_line("dynamic OpenGL hook glTexParameteri original=%p replacement=%p",
                 original, hooked_gl_tex_parameter_i);
        return reinterpret_cast<FARPROC>(hooked_gl_tex_parameter_i);
    }
    if (std::strcmp(name, "glTexParameterf") == 0) {
        if (!original) return nullptr;
        real_gl_tex_parameter_f = reinterpret_cast<GlTexParameterfFn>(original);
        return reinterpret_cast<FARPROC>(hooked_gl_tex_parameter_f);
    }
    if (std::strcmp(name, "glTexParameterfv") == 0) {
        if (!original) return nullptr;
        real_gl_tex_parameter_fv = reinterpret_cast<GlTexParameterfvFn>(original);
        return reinterpret_cast<FARPROC>(hooked_gl_tex_parameter_fv);
    }
    if (std::strcmp(name, "glTexParameteriv") == 0) {
        if (!original) return nullptr;
        real_gl_tex_parameter_iv = reinterpret_cast<GlTexParameterivFn>(original);
        return reinterpret_cast<FARPROC>(hooked_gl_tex_parameter_iv);
    }
    if (std::strcmp(name, "glDrawBuffer") == 0) {
        if (!original) return nullptr;
        real_gl_draw_buffer = reinterpret_cast<GlDrawBufferFn>(original);
        return reinterpret_cast<FARPROC>(hooked_gl_draw_buffer);
    }
    if (std::strcmp(name, "glReadBuffer") == 0) {
        if (!original) return nullptr;
        real_gl_read_buffer = reinterpret_cast<GlReadBufferFn>(original);
        return reinterpret_cast<FARPROC>(hooked_gl_read_buffer);
    }
    if (std::strcmp(name, "glPixelZoom") == 0) {
        if (!original) return nullptr;
        real_gl_pixel_zoom = reinterpret_cast<GlPixelZoomFn>(original);
        return reinterpret_cast<FARPROC>(hooked_gl_pixel_zoom);
    }
    if (std::strcmp(name, "glDrawPixels") == 0) {
        if (!original) return nullptr;
        real_gl_draw_pixels = reinterpret_cast<GlDrawPixelsFn>(original);
        return reinterpret_cast<FARPROC>(hooked_gl_draw_pixels);
    }
    if (std::strcmp(name, "glTexImage2D") == 0) {
        if (!original) return nullptr;
        real_gl_tex_image_2d = reinterpret_cast<GlTexImage2DFn>(original);
        return reinterpret_cast<FARPROC>(hooked_gl_tex_image_2d);
    }
    if (std::strcmp(name, "glTexSubImage2D") == 0) {
        if (!original) return nullptr;
        real_gl_tex_sub_image_2d = reinterpret_cast<GlTexSubImage2DFn>(original);
        return reinterpret_cast<FARPROC>(hooked_gl_tex_sub_image_2d);
    }
    if (std::strcmp(name, "SwapBuffers") == 0 || std::strcmp(name, "wglSwapBuffers") == 0) {
        if (!original)
            original = ::GetProcAddress(GetModuleHandleA("gdi32.dll"), "SwapBuffers");
        if (!real_swap_buffers) real_swap_buffers = reinterpret_cast<SwapBuffersFn>(original);
        return reinterpret_cast<FARPROC>(hooked_swap_buffers);
    }
    return original;
}

}  // namespace

bool install_graphics_hooks() {
    MONITORINFO monitor_info{sizeof(monitor_info)};
    HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoA(monitor, &monitor_info)) return false;
    monitor_rect = monitor_info.rcMonitor;
    output_viewport = proportional_4x3_viewport(monitor_rect.right - monitor_rect.left,
                                                monitor_rect.bottom - monitor_rect.top);
    render_width = output_viewport.width;
    render_height = output_viewport.height;
    if (g_config.logical_width || g_config.logical_height) {
        const bool valid = g_config.logical_width >= 640 && g_config.logical_height >= 480 &&
            static_cast<long long>(g_config.logical_width) * 3 ==
                static_cast<long long>(g_config.logical_height) * 4;
        if (!valid) {
            log_line("invalid logical resolution %dx%d; exact 4:3 is required",
                     g_config.logical_width, g_config.logical_height);
            return false;
        }
        render_width = g_config.logical_width;
        render_height = g_config.logical_height;
    }
    auto set_dpi = reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(
        ::GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext"));
    if (set_dpi) {
        const BOOL dpi_result = set_dpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        log_line("per-monitor DPI awareness result=%d error=%lu", dpi_result ? 1 : 0,
                 dpi_result ? ERROR_SUCCESS : GetLastError());
    }

    bool ok = true;
    ok &= patch_iat("KERNEL32.dll", "GetProcAddress",
                    reinterpret_cast<void*>(hooked_get_proc_address),
                    reinterpret_cast<void**>(&real_get_proc_address));
    ok &= patch_iat("USER32.dll", "CreateWindowExA",
                    reinterpret_cast<void*>(hooked_create_window_ex_a),
                    reinterpret_cast<void**>(&real_create_window_ex_a));
    ok &= patch_iat("USER32.dll", "SetWindowPos",
                    reinterpret_cast<void*>(hooked_set_window_pos),
                    reinterpret_cast<void**>(&real_set_window_pos));
    ok &= patch_iat("USER32.dll", "GetSystemMetrics",
                    reinterpret_cast<void*>(hooked_get_system_metrics),
                    reinterpret_cast<void**>(&real_get_system_metrics));
    patch_iat("USER32.dll", "GetClientRect", reinterpret_cast<void*>(hooked_get_client_rect),
              reinterpret_cast<void**>(&real_get_client_rect));
    ok &= patch_iat("USER32.dll", "ChangeDisplaySettingsA",
                    reinterpret_cast<void*>(hooked_change_display_settings_a),
                    reinterpret_cast<void**>(&real_change_display_settings_a));
    ok &= install_bink_hooks();
    ok &= install_movie_request_audit();
    ok &= install_cutscene_input_reuse_guard();
    ok &= install_movie_skip_gate();
    patch_iat("GDI32.dll", "SwapBuffers", reinterpret_cast<void*>(hooked_swap_buffers),
              reinterpret_cast<void**>(&real_swap_buffers));
    patch_iat("GDI32.dll", "ChoosePixelFormat", reinterpret_cast<void*>(hooked_choose_pixel_format),
              reinterpret_cast<void**>(&real_choose_pixel_format));
    real_wgl_delete_context = reinterpret_cast<WglDeleteContextFn>(
        ::GetProcAddress(GetModuleHandleA("opengl32.dll"), "wglDeleteContext"));
    real_wgl_make_current = reinterpret_cast<WglMakeCurrentFn>(
        ::GetProcAddress(GetModuleHandleA("opengl32.dll"), "wglMakeCurrent"));
    // alone4.exe deliberately loads every OpenGL/WGL entry point through
    // KERNEL32!GetProcAddress and has no OPENGL32 import descriptor.  The
    // dynamic hook above is therefore the authoritative path; requiring an
    // OPENGL32 IAT patch here would reject the real game while only succeeding
    // in synthetic programs that link OpenGL at build time.
    hooks_initialized = ok;
    log_line("graphics hooks target monitor=%ld,%ld..%ld,%ld viewport=%d,%d %dx%d logical=%dx%d",
             monitor_rect.left, monitor_rect.top, monitor_rect.right, monitor_rect.bottom,
             output_viewport.x, output_viewport.y, output_viewport.width, output_viewport.height,
             render_width, render_height);
    return ok;
}

bool get_renderer_diagnostics(RendererDiagnostics* diagnostics) {
    if (!diagnostics || diagnostics->size < sizeof(RendererDiagnostics)) return false;
    RendererDiagnostics result{};
    result.initialized = hooks_initialized ? 1 : 0;
    result.framebuffer_ready = framebuffer_ready ? 1 : 0;
    result.physical_width = monitor_rect.right - monitor_rect.left;
    result.physical_height = monitor_rect.bottom - monitor_rect.top;
    result.render_width = render_width;
    result.render_height = render_height;
    result.viewport_x = output_viewport.x;
    result.viewport_y = output_viewport.y;
    result.viewport_width = output_viewport.width;
    result.viewport_height = output_viewport.height;
    result.gl_major = gl_major;
    result.gl_minor = gl_minor;
    result.samples = actual_samples;
    result.anisotropy = static_cast<DWORD>(std::lround(max_anisotropy));
    result.color_bits = actual_color_bits;
    result.alpha_bits = actual_alpha_bits;
    result.depth_bits = actual_depth_bits;
    result.stencil_bits = actual_stencil_bits;
    result.bink_hooks_ready = bink_hooks_ready ? 1 : 0;
    result.bink_source_width = last_bink_source_width;
    result.bink_source_height = last_bink_source_height;
    result.bink_output_width = last_bink_output_width;
    result.bink_output_height = last_bink_output_height;
    result.bink_output_x = last_bink_output_x;
    result.bink_output_y = last_bink_output_y;
    result.crt_enabled = g_config.crt_enabled ? 1 : 0;
    result.crt_ready = crt_ready ? 1 : 0;
    result.crt_signal_width = g_config.crt_signal_width;
    result.crt_signal_height = g_config.crt_signal_height;
    result.bink_presented_frames = bink_presented_frames;
    result.bink_native_blits_suppressed = bink_native_blits_suppressed;
    result.bink_last_surface_format = last_bink_copy_flags & bink_surface_mask;
    result.bink_last_pitch = last_bink_pitch;
    *diagnostics = result;
    return true;
}

bool request_renderer_capture(DWORD flags) {
    flags &= renderer_capture_raw | renderer_capture_output;
    if (!flags || !framebuffer_ready) return false;
    InterlockedOr(&capture_request_flags, static_cast<LONG>(flags));
    return true;
}

}  // namespace aitd4
