// Legacy OpenGL call correction for viewport, textures and pixel operations.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

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
