// Movie request auditing and the player input gate used while FMVs run.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

const char* movie_request_name(int id, char (&name)[9]) {
    if (id < 0 || id >= 64) return "<invalid>";
    auto* image = static_cast<std::uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
    const char* entry = reinterpret_cast<const char*>(image + (is_retail_executable() ? 0x103628 : 0x103728) + id * 10);
    std::memcpy(name, entry, 8);
    name[8] = '\0';
    return name[0] ? name : "<empty>";
}

void __cdecl hooked_movie_request(int id, std::uint32_t skip_mask) {
    const int previous_id = current_movie_request_id;
    const bool previous_opened = current_movie_request_opened;
    const std::uint32_t previous_serial = current_movie_request_serial;
    current_movie_request_id = id;
    current_movie_request_opened = false;
    const std::uint32_t serial = ++movie_request_serial;
    current_movie_request_serial = serial;
    char name[9]{};
    log_line("FMV ledger serial=%u event=request id=%d name=%s skip_mask=%08X", serial, id,
             movie_request_name(id, name), skip_mask);
    real_movie_request(id, skip_mask);
    log_line("FMV ledger serial=%u event=request-complete id=%d opened=%s", serial, id,
             current_movie_request_opened ? "yes" : "no");
    current_movie_request_id = previous_id;
    current_movie_request_opened = previous_opened;
    current_movie_request_serial = previous_serial;
}

bool install_movie_request_audit() {
#ifdef AITD4_TEST_HARNESS
    log_line("FMV request audit hook skipped in test harness");
    return true;
#else
    auto* image = static_cast<std::uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
    auto* target = image + (is_retail_executable() ? 0x810BF : 0x812CF);
    constexpr std::size_t prologue_size = 9;
    constexpr std::uint8_t expected[prologue_size]{
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x94, 0x00, 0x00, 0x00};
    if (std::memcmp(target, expected, sizeof(expected)) != 0) {
        log_line("FMV request hook signature mismatch address=%p", target);
        return false;
    }

    auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, prologue_size + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) return false;
    std::memcpy(trampoline, target, prologue_size);
    trampoline[prologue_size] = 0xE9;
    const auto return_displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(target + prologue_size) -
        (reinterpret_cast<std::intptr_t>(trampoline + prologue_size) + 5));
    std::memcpy(trampoline + prologue_size + 1, &return_displacement,
                sizeof(return_displacement));
    FlushInstructionCache(GetCurrentProcess(), trampoline, prologue_size + 5);
    real_movie_request = reinterpret_cast<MovieRequestFn>(trampoline);

    std::uint8_t replacement[prologue_size]{0xE9};
    const auto hook_displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(hooked_movie_request) -
        (reinterpret_cast<std::intptr_t>(target) + 5));
    std::memcpy(replacement + 1, &hook_displacement, sizeof(hook_displacement));
    std::fill(replacement + 5, replacement + prologue_size,
              static_cast<std::uint8_t>(0x90));
    if (!write_code_patch(target, replacement, sizeof(replacement))) return false;
    log_line("FMV request audit hook installed address=%p trampoline=%p", target, trampoline);
    return true;
#endif
}

void* WINAPI hooked_bink_buffer_open(HWND window, std::uint32_t width, std::uint32_t height,
                                     std::uint32_t flags) {
    void* buffer = real_bink_buffer_open(window, width, height, flags);
    last_bink_source_width = width;
    last_bink_source_height = height;
    last_bink_output_width = static_cast<std::uint32_t>(MulDiv(
        static_cast<int>(width), output_viewport.width, 640));
    last_bink_output_height = static_cast<std::uint32_t>(MulDiv(
        static_cast<int>(height), output_viewport.height, 480));
    if (buffer) {
        active_bink_buffer = buffer;
        active_bink_canvas_x = std::max(0, (640 - static_cast<int>(width)) / 2);
        active_bink_canvas_y = std::max(0, (480 - static_cast<int>(height)) / 2);
        last_bink_destination = nullptr;
        last_bink_pitch = 0;
        last_bink_copy_height = 0;
        last_bink_copy_flags = 0;
        last_bink_copy_x = 0;
        last_bink_copy_y = 0;
        artificial_bink_lock = false;
        if (!g_config.crt_enabled &&
            !real_bink_buffer_set_scale(buffer, last_bink_output_width, last_bink_output_height))
            fatal_graphics("Native Bink rejected the proportional FMV scale. The game will not continue with misaligned video.");
        log_line("Bink movie opened buffer=%p source=%ux%u output=%ux%u presentation=%s",
                 buffer, width, height, last_bink_output_width, last_bink_output_height,
                 g_config.crt_enabled ? "native-canvas-crt" : "native-scaled-blit");
    }
    return buffer;
}

int WINAPI hooked_bink_buffer_set_offset(void* buffer, int x, int y) {
    if (buffer == active_bink_buffer) {
        active_bink_canvas_x = x;
        active_bink_canvas_y = y;
    }
    last_bink_output_x = output_viewport.x + MulDiv(x, output_viewport.width, 640);
    last_bink_output_y = output_viewport.y + MulDiv(y, output_viewport.height, 480);
    log_line("Bink movie offset buffer=%p source=%d,%d output=%d,%d", buffer, x, y,
             last_bink_output_x, last_bink_output_y);
    return g_config.crt_enabled
        ? real_bink_buffer_set_offset(buffer, x, y)
        : real_bink_buffer_set_offset(buffer, last_bink_output_x, last_bink_output_y);
}

void* WINAPI hooked_bink_open(const char* filename, std::uint32_t flags) {
    const bool readable_name = filename && !IsBadStringPtrA(filename, MAX_PATH);
    void* bink = real_bink_open(filename, flags);
    const char* error = !bink && real_bink_get_error ? real_bink_get_error() : nullptr;
    log_line("BinkOpen file=%s pointer=%p flags=%08X result=%p error=%s",
             readable_name ? filename : "<unreadable>", filename, flags, bink,
             error ? error : "-");
    // alone4.exe intentionally retries BinkOpen with an alternate path after a null result.
    // Preserve that native control flow; the log records both attempts and the final Bink error.
    if (bink) InterlockedExchange(&fmv_pixel_trace_budget, 256);
    if (bink) {
        current_movie_request_opened = true;
        movie_skip_gate.reset();
        active_bink_movie = bink;
        active_bink_request_id = current_movie_request_id;
        active_bink_request_serial = current_movie_request_serial;
        active_bink_do_frames = 0;
        active_bink_copies = 0;
        active_bink_next_frames = 0;
        active_bink_presented_frames = 0;
        active_bink_suppressed_blits = 0;
        active_bink_buffer = nullptr;
        artificial_bink_lock = false;
        alternate_bink_frame_pending = false;
        last_bink_source_width = 0;
        last_bink_source_height = 0;
        last_bink_output_width = 0;
        last_bink_output_height = 0;
        last_bink_destination = nullptr;
        last_bink_pitch = 0;
        last_bink_copy_height = 0;
        last_bink_copy_flags = 0;
        log_line("FMV ledger serial=%u event=open id=%d movie=%p",
                 active_bink_request_serial, active_bink_request_id, bink);
    }
    return bink;
}

std::uint32_t __fastcall hooked_movie_skip_input(void* input_state, void*) {
    const auto* state = static_cast<const std::uint8_t*>(input_state);
    const std::uint32_t pressed = *reinterpret_cast<const std::uint32_t*>(state + 8);
    const std::uint32_t held = *reinterpret_cast<const std::uint32_t*>(state + 0x10);
    const bool was_armed = movie_skip_gate.armed();
    const std::uint32_t filtered = movie_skip_gate.filter(pressed, held);
    if (!was_armed && movie_skip_gate.armed())
        log_line("movie skip gate armed after neutral input held=%08X pressed=%08X", held, pressed);
    else if ((pressed & MovieSkipGate::skip_mask) &&
             !(filtered & MovieSkipGate::skip_mask))
        log_line("movie skip edge suppressed without a held-state rising edge held=%08X pressed=%08X", held, pressed);
    else if (was_armed && (filtered & MovieSkipGate::skip_mask))
        log_line("movie skip edge accepted held=%08X pressed=%08X", held, pressed);
    return filtered;
}

bool install_movie_skip_gate() {
#ifdef AITD4_TEST_HARNESS
    log_line("movie skip callsite hook skipped in test harness");
    return true;
#else
    auto* image = static_cast<std::uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
    auto* call = image + (is_retail_executable() ? 0x9DD7F : 0x9DEDF);
    constexpr std::uint8_t expected[5]{0xE8, 0x1E, 0x5F, 0x00, 0x00};
    if (std::memcmp(call, expected, sizeof(expected)) != 0) {
        log_line("movie skip callsite signature mismatch address=%p", call);
        return false;
    }
    std::uint8_t replacement[5]{0xE8};
    const auto displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(hooked_movie_skip_input) -
        (reinterpret_cast<std::intptr_t>(call) + 5));
    std::memcpy(replacement + 1, &displacement, sizeof(displacement));
    DWORD old_protection = 0;
    if (!VirtualProtect(call, sizeof(replacement), PAGE_EXECUTE_READWRITE, &old_protection))
        return false;
    std::memcpy(call, replacement, sizeof(replacement));
    DWORD ignored = 0;
    VirtualProtect(call, sizeof(replacement), old_protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), call, sizeof(replacement));
    log_line("movie skip release-gate hook installed address=%p", call);
    return true;
#endif
}
