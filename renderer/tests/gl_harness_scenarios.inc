// Presentation scenarios, assertions and harness shutdown.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

    if (full_scissor[0] != 0 || full_scissor[1] != 0 ||
        full_scissor[2] != static_cast<GLint>(status.render_width) ||
        full_scissor[3] != static_cast<GLint>(status.render_height)) return 25;
    const GLint expected_viewport_x = MulDiv(11, status.render_width, 1280);
    const GLint expected_viewport_y = MulDiv(13, status.render_height, 960);
    const GLint expected_viewport_right = MulDiv(712, status.render_width, 1280);
    const GLint expected_viewport_top = MulDiv(516, status.render_height, 960);
    const GLint expected_scissor_x = MulDiv(17, status.render_width, 1280);
    const GLint expected_scissor_y = MulDiv(19, status.render_height, 960);
    const GLint expected_scissor_right = MulDiv(618, status.render_width, 1280);
    const GLint expected_scissor_top = MulDiv(420, status.render_height, 960);
    FILETIME capture_threshold{};
    GetSystemTimeAsFileTime(&capture_threshold);
    if (!capture(aitd4::renderer_capture_raw | aitd4::renderer_capture_output)) return 29;
    for (int frame = 0; frame < 4; ++frame) {
        glClearColor(0.03f, 0.06f, 0.09f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.0f, 0.0f); glVertex2f(-0.8f, -0.8f);
        glColor3f(0.0f, 1.0f, 0.0f); glVertex2f(0.8f, -0.8f);
        glColor3f(0.0f, 0.0f, 1.0f); glVertex2f(0.0f, 0.8f);
        glEnd();
        glEnable(GL_BLEND);
        glEnable(GL_FOG);
        glMatrixMode(GL_TEXTURE);
        viewport(11, 13, 701, 503);
        scissor(17, 19, 601, 401);
        if (frame == 3) {
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glEnable(GL_COLOR_LOGIC_OP);
            glLogicOp(GL_XOR);
        }
        if (!swap_buffers(dc)) return 15;
        GLint matrix_mode = 0;
        GLint restored_viewport[4]{};
        GLint restored_scissor[4]{};
        GLint restored_texture = 0;
        glGetIntegerv(GL_MATRIX_MODE, &matrix_mode);
        glGetIntegerv(GL_VIEWPORT, restored_viewport);
        glGetIntegerv(GL_SCISSOR_BOX, restored_scissor);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &restored_texture);
        if (!glIsEnabled(GL_BLEND) || !glIsEnabled(GL_FOG) || matrix_mode != GL_TEXTURE ||
            restored_texture != static_cast<GLint>(texture) ||
            restored_viewport[0] != expected_viewport_x ||
            restored_viewport[1] != expected_viewport_y ||
            restored_viewport[2] != expected_viewport_right - expected_viewport_x ||
            restored_viewport[3] != expected_viewport_top - expected_viewport_y ||
            restored_scissor[0] != expected_scissor_x ||
            restored_scissor[1] != expected_scissor_y ||
            restored_scissor[2] != expected_scissor_right - expected_scissor_x ||
            restored_scissor[3] != expected_scissor_top - expected_scissor_y) return 26;
        if (frame == 3) {
            GLboolean color_mask[4]{};
            GLint polygon_mode[2]{};
            glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
            glGetIntegerv(GL_POLYGON_MODE, polygon_mode);
            if (color_mask[0] || color_mask[1] || color_mask[2] || color_mask[3] ||
                polygon_mode[0] != GL_LINE || polygon_mode[1] != GL_LINE ||
                !glIsEnabled(GL_COLOR_LOGIC_OP)) return 30;
        }
        if (frame == 0) {
            const std::string raw_capture = find_new_capture(build_directory, "raw", capture_threshold);
            const std::string output_capture = find_new_capture(build_directory, "output", capture_threshold);
            unsigned char raw_sample[4]{};
            bool valid = !raw_capture.empty() && !output_capture.empty() &&
                read_tga_sample(raw_capture, status.render_width, status.render_height,
                                status.render_width / 2, status.render_height / 2,
                                raw_sample);
            const auto sample_is = [&](int x, int y, bool expect_black) {
                unsigned char pixel[4]{};
                if (!read_tga_sample(output_capture, status.physical_width, status.physical_height,
                                     x, y, pixel)) return false;
                const bool black = pixel[0] <= 1 && pixel[1] <= 1 && pixel[2] <= 1;
                return black == expect_black;
            };
            const int center_x = status.physical_width / 2;
            const int center_y = status.physical_height / 2;
            valid = valid && sample_is(center_x, center_y, false) &&
                sample_is(status.viewport_x, center_y, false) &&
                sample_is(status.viewport_x + status.viewport_width - 1, center_y, false) &&
                sample_is(center_x, status.viewport_y, false) &&
                sample_is(center_x, status.viewport_y + status.viewport_height - 1, false);
            if (status.viewport_x > 0)
                valid = valid && sample_is(0, center_y, true) &&
                    sample_is(status.viewport_x - 1, center_y, true);
            if (status.viewport_y > 0)
                valid = valid && sample_is(center_x, 0, true) &&
                    sample_is(center_x, status.viewport_y - 1, true);
            if (!valid) return 28;
        }
    }
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_COLOR_LOGIC_OP);
    SendMessageA(window, WM_ACTIVATEAPP, FALSE, 0);
    SendMessageA(window, WM_ACTIVATEAPP, TRUE, 0);
    LARGE_INTEGER frequency{};
    LARGE_INTEGER timing_start{};
    LARGE_INTEGER timing_end{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&timing_start);
    constexpr int timed_frames = 18;
    for (int frame = 0; frame < timed_frames; ++frame) {
        viewport(0, 0, static_cast<GLsizei>(status.render_width),
                 static_cast<GLsizei>(status.render_height));
        glClearColor(0.18f, 0.18f, 0.18f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        if (!swap_buffers(dc)) return 38;
    }
    QueryPerformanceCounter(&timing_end);
    const double timed_seconds = static_cast<double>(timing_end.QuadPart - timing_start.QuadPart) /
                                 static_cast<double>(frequency.QuadPart);
    if (timed_seconds > 6.0) return 39;
    if (!diagnostics(&status) || !status.initialized || !status.framebuffer_ready) return 16;
    if (status.gl_major < 3 || (status.gl_major == 3 && status.gl_minor < 3)) return 17;
    if (status.samples != 4 || status.color_bits < 32 || status.alpha_bits < 8 ||
        status.depth_bits < 24 ||
        status.stencil_bits < 8) return 18;
    if (!status.crt_enabled || !status.crt_ready || status.crt_signal_width != 640 ||
        status.crt_signal_height != 480) return 40;
    if (status.render_width * 3 != status.render_height * 4 ||
        status.viewport_width * 3 != status.viewport_height * 4) return 19;
    if (status.viewport_x * 2 + static_cast<LONG>(status.viewport_width) !=
            static_cast<LONG>(status.physical_width) ||
        status.viewport_y * 2 + static_cast<LONG>(status.viewport_height) !=
            static_cast<LONG>(status.physical_height)) return 20;
    std::printf("renderer harness passed gl=%lu.%lu logical=%lux%lu physical=%lux%lu "
                "viewport=%ld,%ld %lux%lu samples=%lu aniso=%lu format=%lu/%lu/%lu/%lu "
                "crt_ms_per_frame=%.3f\n",
                status.gl_major, status.gl_minor, status.render_width, status.render_height,
                status.physical_width, status.physical_height, status.viewport_x,
                status.viewport_y, status.viewport_width, status.viewport_height,
                status.samples, status.anisotropy, status.color_bits, status.alpha_bits,
                status.depth_bits, status.stencil_bits,
                timed_seconds * 1000.0 / timed_frames);
    make_current(nullptr, nullptr);
    delete_context(context);
    aitd4::RendererDiagnostics deleted_status{};
    if (!diagnostics(&deleted_status) || deleted_status.framebuffer_ready) return 33;
    context = create_context(dc);
    if (!context || !make_current(dc, context)) return 34;
    glClearColor(0.01f, 0.02f, 0.03f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT);
    if (!swap_buffers(dc)) return 35;
    aitd4::RendererDiagnostics recreated_status{};
    if (!diagnostics(&recreated_status) || !recreated_status.framebuffer_ready) return 36;
    make_current(nullptr, nullptr);
    delete_context(context);
    ReleaseDC(window, dc);
    DestroyWindow(window);
    return 0;
}
