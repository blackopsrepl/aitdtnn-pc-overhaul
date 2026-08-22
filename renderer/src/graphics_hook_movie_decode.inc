// Bink decode bookkeeping and decoded-frame presentation.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

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
