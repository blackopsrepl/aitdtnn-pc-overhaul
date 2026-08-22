// Pixel-format selection, OpenGL context creation and compositor program setup.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

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
