// Harness window, OpenGL context and renderer DLL setup.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    if (!required_change_display_import) return 31;
    char executable[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, executable, MAX_PATH)) return 1;
    char* separator = std::strrchr(executable, '\\');
    if (!separator) return 2;
    char build_directory[MAX_PATH]{};
    strcpy_s(build_directory, executable);
    *std::strrchr(build_directory, '\\') = '\0';
    strcpy_s(separator + 1, MAX_PATH - static_cast<std::size_t>(separator + 1 - executable),
             "aitd4-renderer-hook-test.dll");
    HMODULE renderer = LoadLibraryA(executable);
    if (!renderer) return 3;
    auto initialize = reinterpret_cast<InitializeFn>(
        lookup(renderer, "AITD4_Initialize"));
    auto diagnostics = reinterpret_cast<DiagnosticsFn>(
        lookup(renderer, "AITD4_GetRendererDiagnostics"));
    auto capture = reinterpret_cast<CaptureFn>(
        lookup(renderer, "AITD4_RequestRendererCapture"));
    if (!initialize || !diagnostics || !capture || !initialize(nullptr) || !initialize(nullptr))
        return 4;
    if (GetSystemMetrics(SM_CXSCREEN) * 3 != GetSystemMetrics(SM_CYSCREEN) * 4) return 37;

    WNDCLASSA cls{};
    cls.style = CS_OWNDC;
    cls.lpfnWndProc = window_proc;
    cls.hInstance = instance;
    cls.lpszClassName = "AITD4 GL Harness";
    if (!RegisterClassA(&cls)) return 10;
    HWND window = CreateWindowExA(0, cls.lpszClassName, "AITD4 GL Harness",
                                  WS_OVERLAPPEDWINDOW, 0, 0, 1280, 960,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) return 11;
    SetWindowPos(window, nullptr, 0, 0, 1280, 960, SWP_NOZORDER | SWP_NOACTIVATE);
    HDC dc = GetDC(window);
    PIXELFORMATDESCRIPTOR format{};
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    format.cDepthBits = 24;
    format.cStencilBits = 8;
    const int index = ChoosePixelFormat(dc, &format);
    if (!index || !SetPixelFormat(dc, index, &format)) return 12;
    HMODULE gl = LoadLibraryA("opengl32.dll");
    HMODULE gdi = GetModuleHandleA("gdi32.dll");
    auto create_context = reinterpret_cast<WglCreateContextFn>(lookup(gl, "wglCreateContext"));
    auto make_current = reinterpret_cast<WglMakeCurrentFn>(lookup(gl, "wglMakeCurrent"));
    auto delete_context = reinterpret_cast<WglDeleteContextFn>(lookup(gl, "wglDeleteContext"));
    auto swap_buffers = reinterpret_cast<SwapBuffersFn>(lookup(gdi, "SwapBuffers"));
    if (!create_context || !make_current || !delete_context || !swap_buffers) return 13;
    HGLRC context = create_context(dc);
    if (!context || !make_current(dc, context)) return 13;
    aitd4::RendererDiagnostics status{};
    if (!diagnostics(&status) || !status.initialized || !status.framebuffer_ready) return 16;
    auto clear = reinterpret_cast<GlClearFn>(lookup(gl, "glClear"));
    auto viewport = reinterpret_cast<GlViewportFn>(lookup(gl, "glViewport"));
    auto scissor = reinterpret_cast<GlScissorFn>(lookup(gl, "glScissor"));
    auto draw_buffer = reinterpret_cast<GlDrawBufferFn>(lookup(gl, "glDrawBuffer"));
    auto read_buffer = reinterpret_cast<GlReadBufferFn>(lookup(gl, "glReadBuffer"));
    auto fogf = reinterpret_cast<GlFogfFn>(lookup(gl, "glFogf"));
    auto texture_parameter = reinterpret_cast<GlTexParameteriFn>(
        lookup(gl, "glTexParameteri"));
    auto texture_parameter_f = reinterpret_cast<GlTexParameterfFn>(
        lookup(gl, "glTexParameterf"));
    if (!clear || !viewport || !scissor || !draw_buffer || !read_buffer || !fogf ||
        !texture_parameter || !texture_parameter_f)
        return 14;
    draw_buffer(GL_BACK);
    read_buffer(GL_BACK);
    viewport(0, 0, static_cast<GLsizei>(status.render_width),
             static_cast<GLsizei>(status.render_height));
    fogf(GL_FOG_DENSITY, 0.25f);
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    texture_parameter(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    texture_parameter(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    constexpr GLenum texture_max_anisotropy = 0x84FE;
    constexpr GLint clamp_to_edge = 0x812F;
    GLint wrap = 0;
    GLfloat anisotropy = 0.0f;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrap);
    glGetTexParameterfv(GL_TEXTURE_2D, texture_max_anisotropy, &anisotropy);
    if (wrap != clamp_to_edge) {
        std::fprintf(stderr, "wrap correction failed actual=%d expected=%d\n", wrap, clamp_to_edge);
        return 21;
    }
    if (anisotropy != 1.0f) {
        std::fprintf(stderr, "flat-filter anisotropy failed actual=%.3f expected=1.0\n", anisotropy);
        return 27;
    }
    texture_parameter(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGetTexParameterfv(GL_TEXTURE_2D, texture_max_anisotropy, &anisotropy);
    if (anisotropy < 4.0f) return 22;
    texture_parameter(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glGetTexParameterfv(GL_TEXTURE_2D, texture_max_anisotropy, &anisotropy);
    if (anisotropy != 1.0f) return 23;
    texture_parameter_f(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        static_cast<GLfloat>(GL_LINEAR_MIPMAP_LINEAR));
    glGetTexParameterfv(GL_TEXTURE_2D, texture_max_anisotropy, &anisotropy);
    if (anisotropy < 4.0f) return 32;
    texture_parameter(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    viewport(0, 0, 1280, 960);
    GLint full_viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, full_viewport);
    if (full_viewport[0] != 0 || full_viewport[1] != 0 ||
        full_viewport[2] != static_cast<GLint>(status.render_width) ||
        full_viewport[3] != static_cast<GLint>(status.render_height)) return 24;
    scissor(0, 0, 1280, 960);
    GLint full_scissor[4]{};
    glGetIntegerv(GL_SCISSOR_BOX, full_scissor);
