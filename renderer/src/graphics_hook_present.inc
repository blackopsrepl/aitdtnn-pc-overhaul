// Final frame presentation, scaling and swap-buffer behavior.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

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
