// Frame capture, fullscreen drawing and off-screen framebuffer construction.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

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
