// Miles sample suppression and Dreamcast sequence dispatch translation.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

aitd4::MilesSample WINAPI hook_miles_allocate_sample(aitd4::MilesDriver driver) {
    const auto sample = g_miles_allocate_sample(driver);
    attach_miles_stream(driver, "allocate_sample_handle");
    return sample;
}

bool install_miles_output_hook() {
    auto module = GetModuleHandleA("mss32.dll");
    aitd4::MilesApi api{};
    if (!module || !aitd4::resolve_miles_api(module, api)) {
        log_line("Miles AICA stream rejected: required Miles 6.1 exports unavailable");
        return false;
    }
    auto** wave_slot = find_main_import("Mss32.dll", "_AIL_waveOutOpen@16");
    auto** allocate_slot = find_main_import("Mss32.dll", "_AIL_allocate_sample_handle@4");
    if (!wave_slot || !allocate_slot) {
        log_line("Miles AICA stream rejected: required game imports unavailable wave=%p allocate=%p",
                 wave_slot, allocate_slot);
        return false;
    }
    g_miles_wave_out_open = reinterpret_cast<MilesWaveOutOpenFn>(*wave_slot);
    g_miles_allocate_sample = reinterpret_cast<MilesAllocateSampleFn>(*allocate_slot);
    g_miles_stream = std::make_unique<aitd4::MilesStream>(api, &aitd4::renderer_render_pcm,
                                                          &stream_log);
    DWORD wave_protection = 0;
    if (!VirtualProtect(wave_slot, sizeof(*wave_slot), PAGE_READWRITE, &wave_protection)) return false;
    *wave_slot = reinterpret_cast<void*>(&hook_miles_wave_out_open);
    DWORD ignored = 0;
    VirtualProtect(wave_slot, sizeof(*wave_slot), wave_protection, &ignored);
    DWORD allocate_protection = 0;
    if (!VirtualProtect(allocate_slot, sizeof(*allocate_slot), PAGE_READWRITE,
                        &allocate_protection)) return false;
    *allocate_slot = reinterpret_cast<void*>(&hook_miles_allocate_sample);
    VirtualProtect(allocate_slot, sizeof(*allocate_slot), allocate_protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), wave_slot, sizeof(*wave_slot));
    FlushInstructionCache(GetCurrentProcess(), allocate_slot, sizeof(*allocate_slot));
    log_line("Miles driver hooks installed waveOutOpen=%p allocate_sample=%p",
             wave_slot, allocate_slot);
    return true;
}

int player_index_from_handle(void* handle, std::uint8_t* image_base) {
    if (!g_profile) return -1;
    auto handles = reinterpret_cast<void**>(image_base + g_profile->handles_rva);
    for (int index = 0; index < sequence_slots; ++index) if (handles[index] == handle) return index;
    return -1;
}

using DispatchFn = int(__cdecl*)(void*, std::uint8_t*);

bool bind_player(int player, const std::uint8_t* inverse_map, int map_selector,
                 int bank_id, const void* sequence_table, const void* sequence_base,
                 const void* sequence_cursor, bool log_failure) {
    if (!aitd4::renderer_ready()) {
        if (log_failure)
            log_line("scene slot=%d bank_id=%d selector=%d map=%p seqtable=%p seqbase=%p cursor=%p "
                     "match=deferred/- evidence=renderer-not-ready candidates=0 bound=no",
                     player, bank_id, map_selector, inverse_map, sequence_table,
                     sequence_base, sequence_cursor);
        return false;
    }

    if (!sequence_table) {
        const bool bound = aitd4::renderer_set_scene(player, "gamesnd", 5);
        if (bound) g_last_container[player] = "gamesnd";
        if (log_failure || bound)
            log_line("scene slot=%d bank_id=%d selector=%d map=%p seqtable=%p seqbase=%p cursor=%p "
                     "match=gamesnd/- evidence=no-sequence candidates=1 bound=%s",
                     player, bank_id, map_selector, inverse_map, sequence_table,
                     sequence_base, sequence_cursor, bound ? "yes" : "no");
        return bound;
    }

    const auto resolution = resolve_container(
        inverse_map, map_selector, bank_id, sequence_base, sequence_table);
    if (!resolution.sequence_identity || !resolution.bank_identity) {
        if (log_failure)
            log_line("scene slot=%d bank_id=%d selector=%d map=%p seqtable=%p seqbase=%p cursor=%p "
                     "match=unresolved/- evidence=%s candidates=%d bound=no",
                     player, bank_id, map_selector, inverse_map, sequence_table,
                     sequence_base, sequence_cursor,
                     music_identity::evidence_name(resolution.evidence), resolution.candidate_count);
        return false;
    }

    const bool bound = aitd4::renderer_set_scene(
        player, resolution.bank_identity->container.c_str(), resolution.bank_identity->bank_id);
    if (bound) g_last_container[player] = resolution.bank_identity->container;
    if (log_failure || bound)
        log_line("scene slot=%d bank_id=%d selector=%d map=%p seqtable=%p seqbase=%p cursor=%p "
                 "sequence=%s/%s bank=%s evidence=%s candidates=%d bound=%s",
                 player, bank_id, map_selector, inverse_map, sequence_table,
                 sequence_base, sequence_cursor,
                 resolution.sequence_identity->container.c_str(),
                 resolution.sequence ? resolution.sequence->name.c_str() : "-",
                 resolution.bank_identity->container.c_str(),
                 music_identity::evidence_name(resolution.evidence), resolution.candidate_count,
                 bound ? "yes" : "no");
    return bound;
}

int __cdecl hook_dispatch(void* handle, std::uint8_t* message) {
    auto image_base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    const int player = player_index_from_handle(handle, image_base);
    int map_selector = -1;
    int bank_id = -1;
    std::uint8_t* inverse_map = nullptr;
    void* sequence_table = nullptr;
    void* sequence_base = nullptr;
    void* sequence_cursor = nullptr;
    if (player >= 0) {
        // alone4.exe has five sequence-state records. 0x5220EC is merely
        // record 0's active flag (state+0x134), not the array base.
        auto state = image_base + g_profile->sequence_state_rva + player * 0x144;
        map_selector = state[0x136];
        bank_id = state[0x137];
        if (bank_id >= 0 && bank_id < 6 && map_selector >= 0 && map_selector < 6)
            inverse_map = *reinterpret_cast<std::uint8_t**>(image_base + g_profile->inverse_maps_rva + bank_id * 0x18 + map_selector * 4);
        sequence_table = *reinterpret_cast<void**>(state + 0x120);
        sequence_base = *reinterpret_cast<void**>(state + 0x124);
        sequence_cursor = *reinterpret_cast<void**>(state + 0x12C);
    }

    // Non-persistent handles are the game's short MIDI SFX. They remain on
    // the original PC dispatcher and never enter the Dreamcast renderer.
    if (player < 0)
        return reinterpret_cast<DispatchFn>(g_dispatch_trampoline)(handle, message);

    const std::uint8_t status = message ? message[0] : 0;
    const std::uint8_t kind = status & 0xF0;
    const std::uint8_t channel = status & 0x0F;
    const auto bank_serial = loaded_bank_serial_for(bank_id);
    const bool state_changed = sequence_table != g_last_sequence_table[player] ||
                               inverse_map != g_last_inverse_map[player] ||
                               bank_id != g_last_bank_id[player] ||
                               map_selector != g_last_map_selector[player] ||
                               bank_serial != g_last_bank_serial[player];
    if (state_changed) {
        // A Manatee player survives bank selection. Stop every outstanding
        // note before replacing its scene so no prior envelope can become the
        // continuous tone reported after a transition.
        aitd4::renderer_stop_player(player);
        g_channel_program[player].fill(0xFF);
        g_renderer_bound[player] = false;
        g_last_container[player].clear();
        g_last_sequence_table[player] = sequence_table;
        g_last_inverse_map[player] = inverse_map;
        g_last_bank_id[player] = bank_id;
        g_last_map_selector[player] = map_selector;
        g_last_bank_serial[player] = bank_serial;
    }

    if (!g_renderer_bound[player]) {
        g_renderer_bound[player] = bind_player(
            player, inverse_map, map_selector, bank_id, sequence_table,
            sequence_base, sequence_cursor, state_changed);
    }

    // Suppress every persistent-player event unless the exact Dreamcast bank
    // is bound. In particular, never reinterpret unresolved scene music with
    // gamesnd: that is a proven wrong-bank/runaway-tone failure path.
    if (!message || !g_renderer_bound[player] || kind < 0x80 || kind > 0xE0)
        return 0;

    // The PC DSEQ parser has already applied its inverse program map before it
    // calls the generic MIDI dispatch (see alone4.exe 0x498F3C..0x498F89).
    // Therefore message[1] on a C0 event is the final local bank program. Do
    // not translate it a second time here.
    if (kind == 0xC0) g_channel_program[player][channel] = message[1];
    const auto remembered_program = g_channel_program[player][channel];
    const int local_program = remembered_program == 0xFF ? -1 : remembered_program;

    if (kind == 0xC0)
        log_line("program slot=%d container=%s local=%u bank_id=%d selector=%d map=%p",
                 player, g_last_container[player].c_str(), message[1],
                 bank_id, map_selector, inverse_map);
    log_line("event slot=%d container=%s status=%02X data1=%02X data2=%02X program=%d",
             player, g_last_container[player].c_str(), status, message[1],
             (kind == 0xC0 || kind == 0xD0) ? 0 : message[2], local_program);

    std::uint8_t normalized[3]{status, message[1],
        (kind == 0xC0 || kind == 0xD0) ? std::uint8_t{0} : message[2]};
    aitd4::renderer_event(player, g_last_container[player].c_str(), local_program, normalized);
    return 0;
}
