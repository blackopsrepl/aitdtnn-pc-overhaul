#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include "audio_renderer.hpp"
#include "midi_lifecycle.hpp"

extern "C" {
#include "arm.h"
#include "dcsound.h"
#include "yam.h"
}

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace aitd4 {
namespace {

struct Engine {
    CRITICAL_SECTION lock{};
    std::vector<std::uint8_t> state;
    std::string root;
    std::array<std::string, 6> slot_bank{};
    std::array<std::uint32_t, 6> slot_size{};
    std::array<int, 6> player_bank{-1, -1, -1, -1, -1, -1};
    std::array<MidiNoteState, 6> active_notes{};
    HWAVEOUT output{};
    static constexpr unsigned frames = 1024;
    static constexpr unsigned buffers = 4;
    std::array<std::array<std::int16_t, frames * 2>, buffers> pcm{};
    std::array<WAVEHDR, buffers> headers{};
    std::atomic_bool ready{};
} g;

std::vector<std::uint8_t> read_file(const std::string& path) {
    FILE* file{};
    if (fopen_s(&file, path.c_str(), "rb") || !file) return {};
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::rewind(file);
    std::vector<std::uint8_t> bytes(size > 0 ? static_cast<std::size_t>(size) : 0);
    if (!bytes.empty() && std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) bytes.clear();
    std::fclose(file);
    return bytes;
}

std::uint32_t u32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

void upload_le(std::uint32_t address, const std::uint8_t* source, std::uint32_t size) {
    std::uint32_t done = 0;
    for (; done + 4 <= size; done += 4)
        dcsound_setword(g.state.data(), address + done, u32(source + done));
    if (done != size) {
        const auto aligned = (address + done) & ~3u;
        auto tail = dcsound_getword(g.state.data(), aligned);
        for (std::uint32_t i = 0; done + i < size; ++i) {
            const auto shift = ((address + done + i) & 3u) * 8u;
            tail = (tail & ~(0xFFu << shift)) | (std::uint32_t(source[done + i]) << shift);
        }
        dcsound_setword(g.state.data(), aligned, tail);
    }
}

std::size_t area_map(std::string_view type) {
    if (type == "SMSB") return 0x14000;
    if (type == "SMPB") return 0x14080;
    if (type == "SOSB") return 0x14100;
    if (type == "SPSR") return 0x14180;
    if (type == "SFPB") return 0x14200;
    if (type == "SFOB") return 0x14280;
    if (type == "SFPW") return 0x14288;
    if (type == "SMDB") return 0x14290;
    return 0;
}

bool install_mlt(const std::vector<std::uint8_t>& mlt) {
    if (mlt.size() < 0x20 || std::memcmp(mlt.data(), "SMLT", 4)) return false;
    std::array<std::uint8_t, 0x298> maps{};
    const auto count = u32(mlt.data() + 8);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto h = 0x20u + i * 0x20u;
        if (h + 0x20 > mlt.size()) return false;
        const std::string type(reinterpret_cast<const char*>(mlt.data() + h), 4);
        const auto map_base = area_map(type);
        if (!map_base) return false;
        const auto bank = u32(mlt.data() + h + 4);
        const auto aica_offset = u32(mlt.data() + h + 8);
        const auto allocation = u32(mlt.data() + h + 12);
        const auto file_offset = static_cast<std::int32_t>(u32(mlt.data() + h + 16));
        const auto file_size = u32(mlt.data() + h + 20);
        const auto map = map_base - 0x14000 + bank * 8;
        if (map + 8 > maps.size()) return false;
        for (unsigned b = 0; b != 4; ++b) {
            maps[map + b] = std::uint8_t(aica_offset >> (b * 8));
            maps[map + 4 + b] = std::uint8_t(allocation >> (b * 8));
        }
        if (file_offset >= 0 && type != "SFPW" && type != "SPSR") {
            if (std::size_t(file_offset) + file_size > mlt.size()) return false;
            upload_le(aica_offset, mlt.data() + file_offset, file_size);
        }
    }
    upload_le(0x14000, maps.data(), static_cast<std::uint32_t>(maps.size()));
    return true;
}

std::uint32_t packed_midi(std::uint8_t status, std::uint8_t data1,
                          std::uint8_t data2, unsigned control) {
    const auto kind = (status >> 4) & 7;
    return ((0xF8u | kind) << 24) | ((control & 7u) << 20) |
           ((status & 15u) << 16) | (std::uint32_t(data1) << 8) | data2;
}

void queue_midi(std::uint8_t status, std::uint8_t data1, std::uint8_t data2, unsigned control) {
    const auto producer = dcsound_getword(g.state.data(), 0x12000) & 0xFFF;
    dcsound_setword(g.state.data(), 0xB000 + producer, packed_midi(status, data1, data2, control));
    dcsound_setword(g.state.data(), 0x12000, (producer + 4) & 0xFFF);
}

void submit_command(unsigned slot, std::initializer_list<std::uint8_t> bytes) {
    std::array<std::uint8_t, 16> command{};
    std::copy(bytes.begin(), bytes.end(), command.begin());
    upload_le(0x12200 + slot * 0x10, command.data(), 0x10);
}

bool execute(std::int16_t* output, unsigned frames) {
    std::uint32_t requested = frames;
    return dcsound_execute(g.state.data(), static_cast<std::int32_t>(frames * 128), output, &requested) >= 0
        && requested == frames;
}

bool load_bank_locked(const char* container, int slot) {
    if (!container || !*container || slot < 0 || slot >= 6) return false;
    if (g.slot_bank[slot] == container) return true;
    auto bank = read_file(g.root + "\\banks\\" + container + ".mpb");
    if (bank.size() < 16 || std::memcmp(bank.data(), "SMPB", 4)) return false;
    const auto map = 0x14080 + slot * 8;
    const auto destination = dcsound_getword(g.state.data(), map);
    if (!destination || destination + bank.size() > 0x1AAC20) return false;
    const auto new_end = destination + std::uint32_t(bank.size());
    std::array<bool, 6> stop_player{};
    for (int old_slot = 0; old_slot != 6; ++old_slot) {
        if (g.slot_bank[old_slot].empty() || !g.slot_size[old_slot]) continue;
        const auto old_start = dcsound_getword(g.state.data(), 0x14080 + old_slot * 8);
        const auto old_end = old_start + g.slot_size[old_slot];
        if (destination < old_end && old_start < new_end) {
            g.slot_bank[old_slot].clear();
            g.slot_size[old_slot] = 0;
            for (int player = 0; player != 6; ++player)
                if (g.player_bank[player] == old_slot) stop_player[player] = true;
        }
    }
    bool queued_stop = false;
    for (int player = 0; player != 6; ++player) {
        if (!stop_player[player]) continue;
        queued_stop |= g.active_notes[player].stop_all(
            [player](std::uint8_t status, std::uint8_t data1, std::uint8_t data2) {
                queue_midi(status, data1, data2, unsigned(player));
            }) != 0;
        g.player_bank[player] = -1;
    }
    if (queued_stop) {
        std::array<std::int16_t, 512 * 2> discarded{};
        if (!execute(discarded.data(), 512)) return false;
    }
    upload_le(destination, bank.data(), static_cast<std::uint32_t>(bank.size()));
    g.slot_bank[slot] = container;
    g.slot_size[slot] = static_cast<std::uint32_t>(bank.size());
    return true;
}

void select_bank_locked(int player, int slot) {
    if (player < 0 || player >= 6 || slot < 0 || slot >= 6 || g.player_bank[player] == slot) return;
    for (unsigned channel = 0; channel != 16; ++channel)
        queue_midi(std::uint8_t(0xB0 | channel), 0x20, std::uint8_t(slot), unsigned(player));
    g.player_bank[player] = slot;
}

void CALLBACK wave_callback(HWAVEOUT output, UINT message, DWORD_PTR, DWORD_PTR parameter, DWORD_PTR) {
    if (message != WOM_DONE || !parameter || !g.ready) return;
    auto* header = reinterpret_cast<WAVEHDR*>(parameter);
    EnterCriticalSection(&g.lock);
    execute(reinterpret_cast<std::int16_t*>(header->lpData), Engine::frames);
    LeaveCriticalSection(&g.lock);
    waveOutWrite(output, header, sizeof(*header));
}

bool open_output() {
    WAVEFORMATEX format{WAVE_FORMAT_PCM, 2, 44100, 44100 * 4, 4, 16, 0};
    if (waveOutOpen(&g.output, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(wave_callback),
                    0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) return false;
    for (unsigned i = 0; i != Engine::buffers; ++i) {
        auto& header = g.headers[i];
        header.lpData = reinterpret_cast<LPSTR>(g.pcm[i].data());
        header.dwBufferLength = static_cast<DWORD>(g.pcm[i].size() * sizeof(std::int16_t));
        if (!execute(g.pcm[i].data(), Engine::frames)) return false;
        if (waveOutPrepareHeader(g.output, &header, sizeof(header)) != MMSYSERR_NOERROR) return false;
        if (waveOutWrite(g.output, &header, sizeof(header)) != MMSYSERR_NOERROR) return false;
    }
    return true;
}

} // namespace

bool renderer_start(const char* root, const char*) {
    InitializeCriticalSection(&g.lock);
    g.root = root ? root : "";
    const auto driver = read_file(g.root + "\\MANATEE.DRV");
    const auto mlt = read_file(g.root + "\\ALONE4.MLT");
    if (driver.size() < 0x20 || std::memcmp(driver.data(), "SDRV", 4) || mlt.empty()) return false;
    if (arm_init() < 0 || yam_init() < 0 || dcsound_init() < 0) return false;
    g.state.resize(dcsound_get_state_size());
    dcsound_clear_state(g.state.data());
    yam_prepare_dynacode(dcsound_get_yam_state(g.state.data()));
    upload_le(0, driver.data() + 0x20, static_cast<std::uint32_t>(driver.size() - 0x20));
    std::array<std::int16_t, 512 * 2> scratch{};
    for (unsigned frames = 0; frames < 22050; frames += 512)
        if (!execute(scratch.data(), 512)) return false;
    if (!install_mlt(mlt)) return false;
    submit_command(0, {0x82, 0, 0});
    submit_command(1, {0x84, 0, 0});
    submit_command(2, {0x81, 0, 0xFF});
    const std::uint8_t ready = 1;
    upload_le(0x12400, &ready, 1);
    if (!execute(scratch.data(), 512)) return false;
    if (!load_bank_locked("gamesnd", 5)) return false;
    for (int player = 0; player != 6; ++player) select_bank_locked(player, 5);
    if (!execute(scratch.data(), 512)) return false;
    g.ready = true;
    if (!open_output()) { g.ready = false; return false; }
    return true;
}

bool renderer_ready() { return g.ready.load(); }

bool renderer_set_scene(int player, const char* container, int bank_slot) {
    if (!g.ready || player < 0 || player >= 6) return false;
    EnterCriticalSection(&g.lock);
    const bool loaded = container && *container && load_bank_locked(container, bank_slot);
    if (loaded)
        select_bank_locked(player, bank_slot);
    LeaveCriticalSection(&g.lock);
    return loaded;
}

void renderer_stop_player(int player) {
    if (!g.ready || player < 0 || player >= 6) return;
    EnterCriticalSection(&g.lock);
    g.active_notes[player].stop_all(
        [player](std::uint8_t status, std::uint8_t data1, std::uint8_t data2) {
            queue_midi(status, data1, data2, unsigned(player));
        });
    LeaveCriticalSection(&g.lock);
}

void renderer_event(int player, const char*, int local_program, const std::uint8_t* message) {
    if (!g.ready || !message || player < 0 || player >= 6) return;
    const auto kind = message[0] & 0xF0;
    if (kind < 0x80 || kind > 0xE0) return;
    // PC bank handles are 4/5 in this executable, whereas ALONE4.MLT exposes
    // the Dreamcast scene banks as slots 0/1 (and gamesnd as 5). The scene
    // identity layer has already issued the correct Manatee CC32 command.
    // Forwarding the PC CC32 would immediately switch the channel back to the
    // wrong bank, which produced the persistent tone heard in every scene.
    if (kind == 0xB0 && message[1] == 0x20) return;
    if ((kind == 0xC0 || (kind == 0x90 && message[2] != 0)) && local_program < 0) return;
    auto data1 = message[1];
    if (kind == 0xC0) data1 = std::uint8_t(local_program);
    EnterCriticalSection(&g.lock);
    if (g.player_bank[player] < 0) { LeaveCriticalSection(&g.lock); return; }
    queue_midi(message[0], data1, message[2], unsigned(player));
    g.active_notes[player].observe(message);
    LeaveCriticalSection(&g.lock);
}

} // namespace aitd4
