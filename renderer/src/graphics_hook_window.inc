// Window sizing, input translation and character-selection control flow.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

LRESULT CALLBACK hooked_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_SIZE && window == game_window) {
        lparam = MAKELPARAM(render_width, render_height);
    }
    return CallWindowProcA(game_wndproc, window, message, wparam, lparam);
}

HWND WINAPI hooked_create_window_ex_a(DWORD ex_style, LPCSTR class_name, LPCSTR window_name,
                                       DWORD style, int x, int y, int width, int height,
                                       HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter) {
    const bool candidate = !game_window && !parent && width >= 640 && height >= 480;
    const bool requested_visible = (style & WS_VISIBLE) != 0;
    if (candidate) {
        style = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        ex_style &= ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);
        x = monitor_rect.left;
        y = monitor_rect.top;
        // The original window procedure receives synchronous creation/size
        // messages before CreateWindowEx returns.  Create at the virtualized
        // 4:3 size first so the game never observes the physical 16:9 client.
        width = render_width;
        height = render_height;
    }
    HWND window = real_create_window_ex_a(ex_style, class_name, window_name, style, x, y,
                                          width, height, parent, menu, instance, parameter);
    if (candidate && window) {
        game_window = window;
        SetLastError(ERROR_SUCCESS);
        game_wndproc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hooked_wndproc)));
        if (!game_wndproc && GetLastError() != ERROR_SUCCESS) {
            log_line("window subclass failed error=%lu", GetLastError());
            game_window = nullptr;
            DestroyWindow(window);
            return nullptr;
        }
        if (!real_set_window_pos(window, HWND_TOP, monitor_rect.left, monitor_rect.top,
                                 monitor_rect.right - monitor_rect.left,
                                 monitor_rect.bottom - monitor_rect.top,
                                 SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
            log_line("physical borderless expansion failed error=%lu", GetLastError());
            DestroyWindow(window);
            game_window = nullptr;
            return nullptr;
        }
        RECT physical_client{};
        ::GetClientRect(window, &physical_client);
        if (requested_visible) ShowWindow(window, SW_SHOW);
        log_line("borderless window=%p title=%s physical=%dx%d logical=%dx%d", window,
                 window_name ? window_name : "", physical_client.right - physical_client.left,
                 physical_client.bottom - physical_client.top, render_width, render_height);
    }
    return window;
}

BOOL WINAPI hooked_set_window_pos(HWND window, HWND insert_after, int x, int y, int width,
                                  int height, UINT flags) {
    if (window == game_window) {
        x = monitor_rect.left;
        y = monitor_rect.top;
        width = monitor_rect.right - monitor_rect.left;
        height = monitor_rect.bottom - monitor_rect.top;
        flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
    }
    return real_set_window_pos(window, insert_after, x, y, width, height, flags);
}

BOOL WINAPI hooked_get_client_rect(HWND window, LPRECT rect) {
    const BOOL result = real_get_client_rect(window, rect);
    if (result && window == game_window) {
        rect->left = 0;
        rect->top = 0;
        rect->right = render_width;
        rect->bottom = render_height;
    }
    return result;
}

int WINAPI hooked_get_system_metrics(int index) {
    switch (index) {
        case SM_CXSCREEN:
        case SM_CXFULLSCREEN:
        case SM_CXVIRTUALSCREEN:
            return render_width;
        case SM_CYSCREEN:
        case SM_CYFULLSCREEN:
        case SM_CYVIRTUALSCREEN:
            return render_height;
        case SM_XVIRTUALSCREEN:
        case SM_YVIRTUALSCREEN:
            return 0;
        case SM_CMONITORS:
            return 1;
        default:
            return real_get_system_metrics(index);
    }
}

LONG WINAPI hooked_change_display_settings_a(DEVMODEA*, DWORD) {
    log_line("exclusive display-mode request suppressed");
    return DISP_CHANGE_SUCCESSFUL;
}

bool write_code_patch(void* address, const void* replacement, std::size_t size) {
    DWORD old_protection = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protection)) return false;
    std::memcpy(address, replacement, size);
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(address, size, old_protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    return restored != FALSE;
}

std::uint32_t __fastcall hooked_get_new_pressed(void* input_state, void*) {
    const auto* state = static_cast<const std::uint8_t*>(input_state);
    const std::uint32_t pressed = *reinterpret_cast<const std::uint32_t*>(state + 8);
    const std::uint32_t held = *reinterpret_cast<const std::uint32_t*>(state + 0x10);
    const bool was_waiting = cutscene_input_guard.waiting_for_neutral();
    const std::uint32_t filtered = cutscene_input_guard.filter(pressed, held);
    if (was_waiting && !cutscene_input_guard.waiting_for_neutral()) {
        log_line("character-selection action guard released after neutral input");
    } else if ((pressed & CutsceneInputGuard::action_mask) &&
               !(filtered & CutsceneInputGuard::action_mask)) {
        log_line("character-selection carried action suppressed held=%08X pressed=%08X",
                 held, pressed);
    }
    return filtered;
}

void __cdecl hooked_character_select_advance() {
    auto* image = static_cast<std::uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
    auto* selection_state = reinterpret_cast<std::uint32_t*>(image + (is_retail_executable() ? 0x102A6C : 0x102B64));
    const std::uint32_t state_before = *selection_state;
    std::uint32_t selected_item = 0;
    if (state_before == 2) {
        void* menu = *reinterpret_cast<void**>(image + (is_retail_executable() ? 0x102A64 : 0x102B5C));
        if (menu) selected_item = real_current_menu_item(menu);
    }
    real_character_select_advance();

    const std::uint32_t state_after = *selection_state;
    if (state_before != 2 || state_after != 3) return;

    auto* input_state = image + (is_retail_executable() ? 0x1BDB08 : 0x1BDC08);
    auto* pressed = reinterpret_cast<std::uint32_t*>(input_state + 8);
    const std::uint32_t carried = *pressed & CutsceneInputGuard::action_mask;
    *pressed &= ~CutsceneInputGuard::action_mask;
    cutscene_input_guard.consume_after_character_selection();
    log_line("character selection confirmed item=%04X state=%u->%u; consumed transition action=%08X",
             selected_item, state_before, state_after, carried);
    const int movie_id = CharacterSelectMovieGate::movie_for_item(selected_item);
    if (movie_id < 0) {
        log_line("FMV ledger serial=0 event=expected-rejected item=%04X reason=unknown-character",
                 selected_item);
        return;
    }

    char name[9]{};
    log_line("FMV ledger serial=0 event=selection-confirmed id=%d name=%s item=%04X stage=portrait-title",
             movie_id, movie_request_name(movie_id, name), selected_item);
}

int __cdecl hooked_post_selection_platform() {
    if (InterlockedCompareExchange(&post_selection_movie_in_flight, 1, 0) == 0) {
        auto* image = static_cast<std::uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
        const std::uint8_t character = *(image + (is_retail_executable() ? 0x101658 : 0x101758));
        const int movie_id = CharacterSelectMovieGate::movie_for_character_byte(character);
        auto* input_state = image + (is_retail_executable() ? 0x1BDB08 : 0x1BDC08);
        auto* pressed = reinterpret_cast<std::uint32_t*>(input_state + 8);
        const std::uint32_t carried = *pressed & CutsceneInputGuard::action_mask;
        *pressed &= ~CutsceneInputGuard::action_mask;
        cutscene_input_guard.consume_after_character_selection();

        const std::uint32_t expected_serial = movie_request_serial + 1;
        char name[9]{};
        log_line("FMV ledger serial=%u event=expected id=%d name=%s character=%u stage=post-portrait-title carried=%08X",
                 expected_serial, movie_id, movie_request_name(movie_id, name), character,
                 carried);
        // Dreamcast invokes SELECT_A/C with an authored skip mask of zero.
        hooked_movie_request(movie_id, 0);
        InterlockedExchange(&post_selection_movie_in_flight, 0);
    } else {
        log_line("post-selection movie request suppressed reason=reentrant");
    }
    return real_post_selection_platform();
}

bool install_cutscene_input_reuse_guard() {
#ifdef AITD4_TEST_HARNESS
    log_line("character-selection input reuse hooks skipped in test harness");
    return true;
#else
    auto* image = static_cast<std::uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
    const bool retail = is_retail_executable();
    auto* getter = image + (retail ? 0xA3CA2 : 0xA3E02);
    auto* selection_call = image + (retail ? 0x7FA6F : 0x7FCE5);
    auto* post_selection_call = image + (retail ? 0x79987 : 0xBAE94);
    auto* post_selection_context = image + (retail ? 0x79976 : 0xBAE83);
    auto* current_item = image + (retail ? 0xA8171 : 0xA82D1);
    auto* confirmation_branch = image + (retail ? 0x7FA98 : 0x7FD0E);
    constexpr std::uint8_t expected_getter[5]{0x55, 0x8B, 0xEC, 0x51, 0x89};
    constexpr std::uint8_t expected_selection_call_15_slot[5]{0xE8, 0x75, 0xAE, 0xFF, 0xFF};
    constexpr std::uint8_t expected_selection_call_retail[5]{0xE8, 0xCB, 0xAF, 0xFF, 0xFF};
    constexpr std::uint8_t expected_post_selection_context_15_slot[29]{
        0x8B, 0x4D, 0xF0, 0x81, 0xE1, 0xFF, 0x00, 0x00, 0x00, 0x85,
        0xC9, 0x0F, 0x84, 0xC1, 0xEC, 0xFB, 0xFF, 0xE8, 0x67, 0x8F,
        0xF4, 0xFF, 0x89, 0x45, 0xE4, 0x83, 0x7D, 0xE4, 0x01};
    constexpr std::uint8_t expected_post_selection_context_retail[29]{
        0x8B, 0x4D, 0xF0, 0x81, 0xE1, 0xFF, 0x00, 0x00, 0x00, 0x85,
        0xC9, 0x0F, 0x84, 0xA9, 0x00, 0x00, 0x00, 0xE8, 0x74, 0xA4,
        0xF8, 0xFF, 0x89, 0x45, 0xE4, 0x83, 0x7D, 0xE4, 0x01};
    constexpr std::uint8_t expected_current_item[8]{
        0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B};
    constexpr std::uint8_t expected_confirmation_branch_15_slot[16]{
        0x8B, 0x0D, 0x5C, 0x2B, 0x50, 0x00, 0xE8, 0xB8,
        0x85, 0x02, 0x00, 0x3D, 0x01, 0x20, 0x00, 0x00};
    constexpr std::uint8_t expected_confirmation_branch_retail[16]{
        0x8B, 0x0D, 0x64, 0x2A, 0x50, 0x00, 0xE8, 0xCE,
        0x86, 0x02, 0x00, 0x3D, 0x01, 0x20, 0x00, 0x00};
    if (std::memcmp(getter, expected_getter, sizeof(expected_getter)) != 0 ||
        std::memcmp(selection_call, retail ? expected_selection_call_retail : expected_selection_call_15_slot, 5) != 0 ||
        std::memcmp(post_selection_context, retail ? expected_post_selection_context_retail : expected_post_selection_context_15_slot, 29) != 0 ||
        std::memcmp(current_item, expected_current_item,
                    sizeof(expected_current_item)) != 0 ||
        std::memcmp(confirmation_branch, retail ? expected_confirmation_branch_retail : expected_confirmation_branch_15_slot, 16) != 0) {
        log_line("character-selection signature mismatch getter=%p confirm=%p post=%p item=%p branch=%p",
                 getter, selection_call, post_selection_call, current_item,
                 confirmation_branch);
        return false;
    }

    real_character_select_advance = reinterpret_cast<CharacterSelectAdvanceFn>(image + (retail ? 0x7AA3F : 0x7AB5F));
    real_post_selection_platform = reinterpret_cast<PostSelectionPlatformFn>(image + 0x3E00);
    real_current_menu_item = reinterpret_cast<CurrentMenuItemFn>(current_item);
    std::uint8_t getter_jump[5]{0xE9};
    const auto getter_displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(hooked_get_new_pressed) -
        (reinterpret_cast<std::intptr_t>(getter) + 5));
    std::memcpy(getter_jump + 1, &getter_displacement, sizeof(getter_displacement));

    std::uint8_t confirm_call[5]{0xE8};
    const auto confirm_displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(hooked_character_select_advance) -
        (reinterpret_cast<std::intptr_t>(selection_call) + 5));
    std::memcpy(confirm_call + 1, &confirm_displacement, sizeof(confirm_displacement));

    std::uint8_t post_selection_finish_call[5]{0xE8};
    const auto post_selection_finish_displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(hooked_post_selection_platform) -
        (reinterpret_cast<std::intptr_t>(post_selection_call) + 5));
    std::memcpy(post_selection_finish_call + 1, &post_selection_finish_displacement,
                sizeof(post_selection_finish_displacement));

    if (!write_code_patch(getter, getter_jump, sizeof(getter_jump)) ||
        !write_code_patch(selection_call, confirm_call, sizeof(confirm_call)) ||
        !write_code_patch(post_selection_call, post_selection_finish_call,
                          sizeof(post_selection_finish_call)))
        return false;
    log_line("character-selection deferred movie hooks installed getter=%p confirm=%p post=%p",
             getter, selection_call, post_selection_call);
    return true;
#endif
}
