#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace aitd4 {

constexpr DWORD renderer_diagnostics_version = 3;
constexpr DWORD renderer_capture_raw = 1;
constexpr DWORD renderer_capture_output = 2;

struct RendererDiagnostics {
    DWORD size{sizeof(RendererDiagnostics)};
    DWORD version{renderer_diagnostics_version};
    DWORD initialized{};
    DWORD framebuffer_ready{};
    DWORD physical_width{};
    DWORD physical_height{};
    DWORD render_width{};
    DWORD render_height{};
    LONG viewport_x{};
    LONG viewport_y{};
    DWORD viewport_width{};
    DWORD viewport_height{};
    DWORD gl_major{};
    DWORD gl_minor{};
    DWORD samples{};
    DWORD anisotropy{};
    DWORD color_bits{};
    DWORD alpha_bits{};
    DWORD depth_bits{};
    DWORD stencil_bits{};
    DWORD bink_hooks_ready{};
    DWORD bink_source_width{};
    DWORD bink_source_height{};
    DWORD bink_output_width{};
    DWORD bink_output_height{};
    LONG bink_output_x{};
    LONG bink_output_y{};
    DWORD crt_enabled{};
    DWORD crt_ready{};
    DWORD crt_signal_width{};
    DWORD crt_signal_height{};
    DWORD bink_presented_frames{};
    DWORD bink_native_blits_suppressed{};
    DWORD bink_last_surface_format{};
    LONG bink_last_pitch{};
};

}  // namespace aitd4
