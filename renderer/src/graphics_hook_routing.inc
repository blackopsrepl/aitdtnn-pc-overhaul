// API lookup routing, hook installation and the public diagnostics interface.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

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
