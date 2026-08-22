// Native movie-frame interception, lifecycle tracking and Bink hook installation.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

bool install_native_movie_frame_hook() {
#ifdef AITD4_TEST_HARNESS
    log_line("native Bink movie-frame callsite hook skipped in test harness");
    return true;
#else
    auto* image = static_cast<std::uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
    auto* call = image + (is_retail_executable() ? 0x9DDD7 : 0x9DF37);
    auto* native_frame = image + (is_retail_executable() ? 0x9DF42 : 0x9E0A2);
    constexpr std::uint8_t expected_call[5]{0xE8, 0x66, 0x01, 0x00, 0x00};
    constexpr std::uint8_t expected_frame_prefix[23]{
        0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x50, 0xFF, 0x15, 0x38, 0xB2, 0x4B,
        0x00, 0x8B, 0x4D, 0x0C, 0x51, 0xFF, 0x15, 0x2C, 0xB2, 0x4B, 0x00};
    constexpr std::uint8_t expected_frame_tail[18]{
        0x8B, 0x55, 0x08, 0x8B, 0x45, 0x08, 0x8B, 0x4A, 0x0C,
        0x3B, 0x48, 0x08, 0x74, 0x0A, 0x8B, 0x55, 0x08, 0x52};
    if (std::memcmp(call, expected_call, sizeof(expected_call)) != 0 ||
        std::memcmp(native_frame, expected_frame_prefix, sizeof(expected_frame_prefix)) != 0 ||
        std::memcmp(native_frame + 0x72, expected_frame_tail, sizeof(expected_frame_tail)) != 0) {
        log_line("native Bink movie-frame signature mismatch call=%p frame=%p", call,
                 native_frame);
        return false;
    }
    real_native_movie_frame = reinterpret_cast<NativeMovieFrameFn>(native_frame);
    std::uint8_t replacement[5]{0xE8};
    const auto displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(hooked_native_movie_frame) -
        (reinterpret_cast<std::intptr_t>(call) + 5));
    std::memcpy(replacement + 1, &displacement, sizeof(displacement));
    if (!write_code_patch(call, replacement, sizeof(replacement))) return false;
    log_line("native Bink movie-frame lifecycle hooked call=%p original=%p", call,
             native_frame);
    return true;
#endif
}

void __cdecl hooked_native_movie_frame(void* bink, void* buffer) {
    if (!g_config.crt_enabled) {
        if (!real_native_movie_frame)
            fatal_graphics("The native Bink movie-frame routine is unavailable.");
        real_native_movie_frame(bink, buffer);
        return;
    }
    if (!bink || bink != active_bink_movie || !buffer ||
        IsBadReadPtr(bink, 0x10) || IsBadReadPtr(buffer, 0x1C)) {
        log_line("CRT native movie-frame precondition rejected movie=%p active_movie=%p buffer=%p",
                 bink, active_bink_movie, buffer);
        fatal_graphics("CRT movie presentation received invalid native movie state.");
    }
    if (active_bink_buffer && active_bink_buffer != buffer)
        fatal_graphics("CRT movie presentation changed native Bink buffers during playback.");
    active_bink_buffer = buffer;

    hooked_bink_do_frame(bink);
    const int locked = real_bink_buffer_lock(buffer);
    bool decoded = false;
    if (locked) {
        auto* bytes = static_cast<std::uint8_t*>(buffer);
        const std::uint32_t height = *reinterpret_cast<const std::uint32_t*>(bytes + 0x04);
        const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(bytes + 0x10);
        void* destination = *reinterpret_cast<void* const*>(bytes + 0x14);
        const std::int32_t pitch = *reinterpret_cast<const std::int32_t*>(bytes + 0x18);
        hooked_bink_copy_to_buffer(bink, destination, pitch, height, 0, 0, flags);
        real_bink_buffer_unlock(buffer);
        decoded = true;
    }

    auto* movie_bytes = static_cast<const std::uint8_t*>(bink);
    const std::uint32_t frame = *reinterpret_cast<const std::uint32_t*>(movie_bytes + 0x0C);
    const std::uint32_t frames = *reinterpret_cast<const std::uint32_t*>(movie_bytes + 0x08);
    const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(
        static_cast<const std::uint8_t*>(buffer) + 0x10);
    // Preserve the native dirty-rectangle query even though its direct window blit is suppressed.
    real_bink_get_rects(bink, flags);
    if (decoded) {
        // This is the exact point where the native routine would issue BinkBufferBlit.
        present_decoded_bink_frame(buffer);
    } else {
        ++bink_native_blits_suppressed;
        ++active_bink_suppressed_blits;
        log_line("native Bink buffer lock returned no frame movie=%p buffer=%p", bink, buffer);
    }
    if (frame != frames) hooked_bink_next_frame(bink);
}

#ifdef AITD4_TEST_HARNESS
extern "C" __declspec(dllexport) DWORD WINAPI AITD4_TestNativeMovieFrame(
    void* bink, void* buffer) {
    hooked_native_movie_frame(bink, buffer);
    return 1;
}
#endif

void WINAPI hooked_bink_buffer_blit(void* buffer, void* rectangles,
                                    std::uint32_t rectangle_count) {
    if (!g_config.crt_enabled) {
        real_bink_buffer_blit(buffer, rectangles, rectangle_count);
        return;
    }
    if (buffer != active_bink_buffer)
        fatal_graphics("CRT Bink blit encountered an untracked presentation buffer.");
    log_line("BinkBufferBlit suppressed after explicit unlock presentation buffer=%p rectangles=%p count=%u",
             buffer, rectangles, rectangle_count);
}

void WINAPI hooked_bink_next_frame(void* bink) {
    real_bink_next_frame(bink);
    if (bink == active_bink_movie) {
        ++active_bink_next_frames;
        if (active_bink_next_frames <= 8 || active_bink_next_frames % 120 == 0)
            log_line("BinkNextFrame movie=%p count=%u", bink, active_bink_next_frames);
    }
}

void WINAPI hooked_bink_close(void* bink) {
    if (bink == active_bink_movie) {
        log_line("FMV ledger serial=%u event=close id=%d movie=%p",
                 active_bink_request_serial, active_bink_request_id, bink);
        log_line("FMV ledger serial=%u event=frames id=%d do=%u copy=%u next=%u presented=%u suppressed=%u",
                  active_bink_request_serial, active_bink_request_id, active_bink_do_frames,
                  active_bink_copies, active_bink_next_frames, active_bink_presented_frames,
                  active_bink_suppressed_blits);
        log_line("BinkClose movie=%p request_id=%d do_frames=%u copies=%u next_frames=%u",
                 bink, active_bink_request_id, active_bink_do_frames, active_bink_copies,
                 active_bink_next_frames);
        active_bink_movie = nullptr;
        active_bink_request_id = -1;
        active_bink_request_serial = 0;
        active_bink_buffer = nullptr;
        last_bink_destination = nullptr;
        artificial_bink_lock = false;
        alternate_bink_frame_pending = false;
    } else {
        log_line("BinkClose movie=%p untracked", bink);
    }
    real_bink_close(bink);
}

bool install_bink_hooks() {
    HMODULE bink = GetModuleHandleA("binkw32.dll");
#ifdef AITD4_TEST_HARNESS
    if (!bink) {
        log_line("Bink integration skipped in non-Bink test harness");
        return true;
    }
#endif
    if (!bink) {
        log_line("required native Bink module is unavailable");
        return false;
    }
    real_bink_buffer_set_scale = reinterpret_cast<BinkBufferSetScaleFn>(
        ::GetProcAddress(bink, "_BinkBufferSetScale@12"));
    real_bink_get_error = reinterpret_cast<BinkGetErrorFn>(
        ::GetProcAddress(bink, "_BinkGetError@0"));
    real_bink_get_rects = reinterpret_cast<BinkGetRectsFn>(
        ::GetProcAddress(bink, "_BinkGetRects@8"));
    bool ok = real_bink_buffer_set_scale != nullptr && real_bink_get_error != nullptr &&
              real_bink_get_rects != nullptr;
    ok &= patch_iat("binkw32.dll", "_BinkOpen@8",
                    reinterpret_cast<void*>(hooked_bink_open),
                    reinterpret_cast<void**>(&real_bink_open));
    ok &= patch_iat("binkw32.dll", "_BinkDoFrame@4",
                    reinterpret_cast<void*>(hooked_bink_do_frame),
                    reinterpret_cast<void**>(&real_bink_do_frame));
    ok &= patch_iat("binkw32.dll", "_BinkCopyToBuffer@28",
                    reinterpret_cast<void*>(hooked_bink_copy_to_buffer),
                    reinterpret_cast<void**>(&real_bink_copy_to_buffer));
    ok &= patch_iat("binkw32.dll", "_BinkBufferBlit@12",
                    reinterpret_cast<void*>(hooked_bink_buffer_blit),
                    reinterpret_cast<void**>(&real_bink_buffer_blit));
    ok &= patch_iat("binkw32.dll", "_BinkBufferLock@4",
                    reinterpret_cast<void*>(hooked_bink_buffer_lock),
                    reinterpret_cast<void**>(&real_bink_buffer_lock));
    ok &= patch_iat("binkw32.dll", "_BinkBufferUnlock@4",
                    reinterpret_cast<void*>(hooked_bink_buffer_unlock),
                    reinterpret_cast<void**>(&real_bink_buffer_unlock));
    ok &= patch_iat("binkw32.dll", "_BinkNextFrame@4",
                    reinterpret_cast<void*>(hooked_bink_next_frame),
                    reinterpret_cast<void**>(&real_bink_next_frame));
    ok &= patch_iat("binkw32.dll", "_BinkClose@4",
                    reinterpret_cast<void*>(hooked_bink_close),
                    reinterpret_cast<void**>(&real_bink_close));
    ok &= patch_iat("binkw32.dll", "_BinkBufferOpen@16",
                    reinterpret_cast<void*>(hooked_bink_buffer_open),
                    reinterpret_cast<void**>(&real_bink_buffer_open));
    ok &= patch_iat("binkw32.dll", "_BinkBufferSetOffset@12",
                    reinterpret_cast<void*>(hooked_bink_buffer_set_offset),
                    reinterpret_cast<void**>(&real_bink_buffer_set_offset));
    ok &= install_native_movie_frame_hook();
    bink_hooks_ready = ok;
    log_line("Bink proportional movie hooks %s", ok ? "ready" : "failed");
    return ok;
}
