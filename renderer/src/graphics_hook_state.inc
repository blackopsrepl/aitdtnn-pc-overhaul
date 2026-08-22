// Shared renderer state, framebuffer cleanup and diagnostic helpers.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

int render_height{};
int source_width{};
int source_height{};
bool inside_compositor{};
bool framebuffer_ready{};
bool hooks_initialized{};
GLuint multisample_fbo{};
GLuint multisample_color{};
GLuint multisample_depth_stencil{};
GLuint resolve_fbo{};
GLuint resolve_texture{};
GLuint compositor_program{};
GLint source_uniform{-1};
GLint inverse_size_uniform{-1};
GLint deband_uniform{-1};
GLint dither_uniform{-1};
GLuint crt_signal_fbo{};
GLuint crt_signal_texture{};
GLuint crt_response_fbo{};
GLuint crt_response_texture{};
GLuint crt_blur_horizontal_fbo{};
GLuint crt_blur_horizontal_texture{};
GLuint crt_blur_vertical_fbo{};
GLuint crt_blur_vertical_texture{};
GLuint bink_canvas_texture{};
GLuint crt_signal_program{};
GLuint crt_response_program{};
GLuint crt_blur_program{};
GLuint crt_present_program{};
GLint crt_signal_source_uniform{-1};
GLint crt_signal_inverse_size_uniform{-1};
GLint crt_signal_deband_uniform{-1};
GLint crt_response_source_uniform{-1};
GLint crt_response_signal_size_uniform{-1};
GLint crt_response_mask_uniform{-1};
GLint crt_response_scanline_uniform{-1};
GLint crt_blur_source_uniform{-1};
GLint crt_blur_inverse_size_uniform{-1};
GLint crt_blur_direction_uniform{-1};
GLint crt_blur_extract_uniform{-1};
GLint crt_present_source_uniform{-1};
GLint crt_present_blur_uniform{-1};
GLint crt_present_bloom_uniform{-1};
GLint crt_present_halation_uniform{-1};
GLint crt_present_dither_uniform{-1};
bool crt_ready{};
float max_anisotropy{1.0f};
int gl_major{};
int gl_minor{};
int actual_samples{1};
int actual_color_bits{};
int actual_alpha_bits{};
int actual_depth_bits{};
int actual_stencil_bits{};
bool pixel_format_logged{};
volatile LONG capture_request_flags{};
int viewport_log_count{};
int scissor_log_count{};
int startup_pixel_log_count{};
volatile LONG fmv_pixel_trace_budget{};
bool framebuffer_srgb_logged{};
bool bink_hooks_ready{};
std::uint32_t last_bink_source_width{};
std::uint32_t last_bink_source_height{};
std::uint32_t last_bink_output_width{};
void* active_bink_buffer{};
int active_bink_canvas_x{};
int active_bink_canvas_y{};
const std::uint8_t* last_bink_destination{};
std::int32_t last_bink_pitch{};
std::uint32_t last_bink_copy_height{};
std::uint32_t last_bink_copy_flags{};
std::uint32_t last_bink_copy_x{};
std::uint32_t last_bink_copy_y{};
std::vector<std::uint8_t> bink_canvas_rgba;
std::vector<std::uint8_t> bink_decode_storage;
std::uint32_t bink_presented_frames{};
std::uint32_t bink_native_blits_suppressed{};
void* active_bink_movie{};
std::uint32_t active_bink_do_frames{};
std::uint32_t active_bink_copies{};
std::uint32_t active_bink_next_frames{};
std::uint32_t active_bink_presented_frames{};
std::uint32_t active_bink_suppressed_blits{};
bool artificial_bink_lock{};
bool alternate_bink_frame_pending{};
int active_bink_request_id{-1};
std::uint32_t active_bink_request_serial{};
int current_movie_request_id{-1};
bool current_movie_request_opened{};
std::uint32_t current_movie_request_serial{};
std::uint32_t movie_request_serial{};
CutsceneInputGuard cutscene_input_guard;
MovieSkipGate movie_skip_gate;
volatile LONG post_selection_movie_in_flight{};
std::uint32_t last_bink_output_height{};
int last_bink_output_x{};
int last_bink_output_y{};

bool initialize_framebuffer();
bool present_crt_texture(GLuint input_texture, int input_width, int input_height,
                         bool apply_deband);
bool capture_frame(const char* label, GLuint framebuffer, GLenum buffer, int width, int height);
void WINAPI hooked_bink_buffer_blit(void* buffer, void* rectangles,
                                    std::uint32_t rectangle_count);
void WINAPI hooked_bink_next_frame(void* bink);
void __cdecl hooked_native_movie_frame(void* bink, void* buffer);
bool install_native_movie_frame_hook();
const char* movie_request_name(int id, char (&name)[9]);
void __cdecl hooked_movie_request(int id, std::uint32_t skip_mask);

void reset_framebuffer_state(bool destroy_objects) {
    const bool was_inside_compositor = inside_compositor;
    inside_compositor = true;
    if (destroy_objects) {
        if (compositor_program && delete_program) delete_program(compositor_program);
        if (crt_signal_program && delete_program) delete_program(crt_signal_program);
        if (crt_response_program && delete_program) delete_program(crt_response_program);
        if (crt_blur_program && delete_program) delete_program(crt_blur_program);
        if (crt_present_program && delete_program) delete_program(crt_present_program);
        const GLuint crt_textures[]{crt_signal_texture, crt_response_texture,
                                    crt_blur_horizontal_texture, crt_blur_vertical_texture,
                                    bink_canvas_texture};
        glDeleteTextures(static_cast<GLsizei>(std::size(crt_textures)), crt_textures);
        const GLuint crt_fbos[]{crt_signal_fbo, crt_response_fbo,
                                crt_blur_horizontal_fbo, crt_blur_vertical_fbo};
        if (delete_framebuffers)
            delete_framebuffers(static_cast<GLsizei>(std::size(crt_fbos)), crt_fbos);
        if (resolve_texture) glDeleteTextures(1, &resolve_texture);
        if (multisample_depth_stencil && delete_renderbuffers)
            delete_renderbuffers(1, &multisample_depth_stencil);
        if (multisample_color && delete_renderbuffers)
            delete_renderbuffers(1, &multisample_color);
        if (resolve_fbo && delete_framebuffers) delete_framebuffers(1, &resolve_fbo);
        if (multisample_fbo && delete_framebuffers) delete_framebuffers(1, &multisample_fbo);
    }
    multisample_fbo = 0;
    multisample_color = 0;
    multisample_depth_stencil = 0;
    resolve_fbo = 0;
    resolve_texture = 0;
    compositor_program = 0;
    source_uniform = -1;
    inverse_size_uniform = -1;
    deband_uniform = -1;
    dither_uniform = -1;
    crt_signal_fbo = crt_signal_texture = 0;
    crt_response_fbo = crt_response_texture = 0;
    crt_blur_horizontal_fbo = crt_blur_horizontal_texture = 0;
    crt_blur_vertical_fbo = crt_blur_vertical_texture = 0;
    bink_canvas_texture = 0;
    crt_signal_program = crt_response_program = crt_blur_program = crt_present_program = 0;
    crt_signal_source_uniform = crt_signal_inverse_size_uniform = crt_signal_deband_uniform = -1;
    crt_response_source_uniform = crt_response_signal_size_uniform = -1;
    crt_response_mask_uniform = crt_response_scanline_uniform = -1;
    crt_blur_source_uniform = crt_blur_inverse_size_uniform = crt_blur_direction_uniform = -1;
    crt_blur_extract_uniform = -1;
    crt_present_source_uniform = crt_present_blur_uniform = -1;
    crt_present_bloom_uniform = crt_present_halation_uniform = crt_present_dither_uniform = -1;
    crt_ready = false;
    framebuffer_ready = false;
    game_context = nullptr;
    inside_compositor = was_inside_compositor;
}

void scale_legacy_rectangle(GLint& x, GLint& y, GLsizei& width, GLsizei& height) {
    if (source_width <= 0 || source_height <= 0 ||
        (source_width == render_width && source_height == render_height)) return;
    const GLint right = x + width;
    const GLint top = y + height;
    const GLint scaled_x = MulDiv(x, render_width, source_width);
    const GLint scaled_y = MulDiv(y, render_height, source_height);
    const GLint scaled_right = MulDiv(right, render_width, source_width);
    const GLint scaled_top = MulDiv(top, render_height, source_height);
    x = scaled_x;
    y = scaled_y;
    width = std::max<GLsizei>(0, scaled_right - scaled_x);
    height = std::max<GLsizei>(0, scaled_top - scaled_y);
}

bool is_graphics_module(HMODULE module) {
    if (!module) return false;
    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(module, path, MAX_PATH)) return false;
    const char* filename = std::strrchr(path, '\\');
    filename = filename ? filename + 1 : path;
    return _stricmp(filename, "opengl32.dll") == 0 || _stricmp(filename, "gdi32.dll") == 0;
}

template <typename T>
bool load_gl_extension(T& output, const char* name) {
    auto wgl_get_proc = reinterpret_cast<PROC(WINAPI*)(LPCSTR)>(
        ::GetProcAddress(GetModuleHandleA("opengl32.dll"), "wglGetProcAddress"));
    output = wgl_get_proc ? reinterpret_cast<T>(wgl_get_proc(name)) : nullptr;
    const auto raw = reinterpret_cast<std::uintptr_t>(output);
    if (raw == 1 || raw == 2 || raw == 3 || raw == static_cast<std::uintptr_t>(-1))
        output = nullptr;
    if (!output) output = reinterpret_cast<T>(
        ::GetProcAddress(GetModuleHandleA("opengl32.dll"), name));
    return output != nullptr;
}

void fatal_graphics(const char* message) {
    log_line("fatal graphics error: %s", message);
#ifndef AITD4_TEST_HARNESS
    MessageBoxA(game_window, message, "AITD4 graphics overhaul",
                MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
#endif
    ExitProcess(91);
}

bool patch_iat(const char* imported_module, const char* symbol, void* replacement,
               void** original) {
    auto base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return false;
    auto descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        const char* module_name = reinterpret_cast<const char*>(base + descriptor->Name);
        if (_stricmp(module_name, imported_module) != 0) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + (descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk
                                                   : descriptor->FirstThunk));
        auto functions = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++functions) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), symbol) != 0) continue;
            DWORD old_protection = 0;
            if (!VirtualProtect(&functions->u1.Function, sizeof(functions->u1.Function),
                                PAGE_READWRITE, &old_protection)) return false;
            if (original && !*original)
                *original = reinterpret_cast<void*>(functions->u1.Function);
            functions->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            DWORD ignored = 0;
            VirtualProtect(&functions->u1.Function, sizeof(functions->u1.Function),
                           old_protection, &ignored);
            FlushInstructionCache(GetCurrentProcess(), &functions->u1.Function,
                                  sizeof(functions->u1.Function));
            log_line("IAT hook %s!%s installed", imported_module, symbol);
            return true;
        }
    }
    return false;
}
