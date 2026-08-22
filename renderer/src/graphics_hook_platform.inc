// Windows, OpenGL and Bink declarations used by every renderer hook.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

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
