#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "audio_hook_api.hpp"
#include "explicit_initializer.hpp"
#include "midi_lifecycle.hpp"
#include "music_identity.hpp"
#include "miles_stream.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

namespace {

struct FakeMiles {
    int allocate_calls{};
    int init_calls{};
    int type{};
    int rate{};
    int volume{};
    int start_calls{};
    int load_calls{};
    int ready{-1};
    aitd4::MilesSampleCallback eob{};
    std::array<const void*, 2> addresses{};
    std::array<std::uint32_t, 2> lengths{};
} fake;

void* WINAPI fake_mem_alloc(std::uint32_t size) { return std::malloc(size); }
void WINAPI fake_mem_free(void* memory) { std::free(memory); }
aitd4::MilesSample WINAPI fake_allocate(aitd4::MilesDriver driver) {
    ++fake.allocate_calls;
    return driver ? reinterpret_cast<void*>(0x2222) : nullptr;
}
void WINAPI fake_release(aitd4::MilesSample) {}
void WINAPI fake_init(aitd4::MilesSample) { ++fake.init_calls; }
void WINAPI fake_type(aitd4::MilesSample, std::int32_t format, std::uint32_t) { fake.type = format; }
void WINAPI fake_rate(aitd4::MilesSample, std::int32_t rate) { fake.rate = rate; }
void WINAPI fake_volume(aitd4::MilesSample, std::int32_t volume) { fake.volume = volume; }
std::int32_t WINAPI fake_ready(aitd4::MilesSample) {
    const auto result = fake.ready;
    fake.ready = -1;
    return result;
}
void WINAPI fake_load(aitd4::MilesSample, std::uint32_t index, const void* address,
                      std::uint32_t length) {
    ++fake.load_calls;
    fake.addresses.at(index) = address;
    fake.lengths.at(index) = length;
}
aitd4::MilesSampleCallback WINAPI fake_register(aitd4::MilesSample,
                                                aitd4::MilesSampleCallback callback) {
    fake.eob = callback;
    return nullptr;
}
void WINAPI fake_start(aitd4::MilesSample) { ++fake.start_calls; }
void WINAPI fake_end(aitd4::MilesSample) {}
bool fake_render(std::int16_t* output, unsigned frames) {
    for (unsigned index = 0; index != frames * 2; ++index)
        output[index] = static_cast<std::int16_t>((index % 127) - 63);
    return true;
}

} // namespace

using AudioInitializeAbi = DWORD (WINAPI*)(void*);
static_assert(std::is_same_v<decltype(&AITD4_AudioInitialize), AudioInitializeAbi>);

int main() {
    {
        aitd4::MilesApi api{&fake_mem_alloc, &fake_mem_free, &fake_allocate,
                            &fake_release, &fake_init, &fake_type, &fake_rate,
                            &fake_volume, &fake_ready, &fake_load, &fake_register,
                            &fake_start, &fake_end};
        aitd4::MilesStream stream(api, &fake_render, nullptr);
        assert(stream.start(reinterpret_cast<void*>(0x1111)));
        assert(fake.allocate_calls == 1 && fake.init_calls == 1 && fake.start_calls == 1);
        assert(fake.type == aitd4::MilesStream::stereo_16_format);
        assert(fake.rate == 44100 && fake.volume == 127 && fake.load_calls == 2);
        assert(fake.addresses[0] != fake.addresses[1]);
        assert(fake.lengths[0] == aitd4::MilesStream::frames_per_buffer * 4);
        assert(stream.rendered_frames() == aitd4::MilesStream::frames_per_buffer * 2);
        assert(stream.peak() > 0 && fake.eob);
        fake.ready = 0;
        fake.eob(reinterpret_cast<void*>(0x2222));
        assert(fake.load_calls == 3);
        assert(stream.rendered_frames() == aitd4::MilesStream::frames_per_buffer * 3);
    }
    {
        std::array<std::uint8_t, 0x24> asset{};
        asset[0] = 'D'; asset[1] = 'S'; asset[2] = 'E'; asset[3] = 'Q';
        asset[0x0E] = 0; asset[0x0F] = 2;
        asset[0x10] = 0; asset[0x11] = 0; asset[0x12] = 0; asset[0x13] = 0x18;
        asset[0x14] = 0; asset[0x15] = 0; asset[0x16] = 0; asset[0x17] = 0x20;
        for (std::size_t index = 0x18; index != asset.size(); ++index)
            asset[index] = static_cast<std::uint8_t>(index);
        auto loaded = asset;
        loaded[0x10] = 0x18; loaded[0x11] = 0; loaded[0x12] = 0; loaded[0x13] = 0;
        loaded[0x14] = 0x20; loaded[0x15] = 0; loaded[0x16] = 0; loaded[0x17] = 0;
        assert(aitd4::music_identity::loaded_dseq_matches(asset, loaded));
        loaded.back() ^= 1;
        assert(!aitd4::music_identity::loaded_dseq_matches(asset, loaded));
    }
    {
        aitd4::music_identity::LoadedIdentityRegistry identities;
        aitd4::music_identity::LoadedBankRegistry banks;
        const auto* base = reinterpret_cast<void*>(0x1000);
        const auto* table = reinterpret_cast<void*>(0x1010);
        identities.record(base, table, "act_c12");
        assert(identities.find(base, table) == "act_c12");
        banks.record(1, "grenier1");
        assert(banks.find(1) == "grenier1");
        assert(banks.serial(1) == 1);
        banks.record(1, "grenier5");
        assert(banks.find(1) == "grenier5");
        assert(banks.serial(1) == 2);
        // A live sequence keeps the container that created its DSEQ object
        // while the shared Dreamcast bank follows the most recent load.
        assert(identities.find(base, table) == "act_c12");
        assert(banks.find(0).empty());
        assert(banks.serial(0) == 0);
        identities.record(base, table, "act_c13");
        assert(identities.find(base, table) == "act_c13");
        assert(identities.find(base, reinterpret_cast<void*>(0x1020)).empty());
        assert(identities.find(reinterpret_cast<void*>(0x2000),
                               reinterpret_cast<void*>(0x2010)).empty());
    }
    {
        const auto aline = aitd4::music_identity::container_from_path(
            R"(C:\Games\AITD4\MIDI\ALINE\Jardin2)");
        const auto carnby = aitd4::music_identity::container_from_path(
            R"(.\midi/carnby/act_c13)");
        assert(aline && *aline == "jardin2");
        assert(carnby && *carnby == "act_c13");
        assert(!aitd4::music_identity::container_from_path(R"(midi\aline\jardin2.tmp)"));
    }
    {
        aitd4::MidiNoteState notes;
        const std::uint8_t on[] = {0x92, 0x35, 0x7F};
        const std::uint8_t off[] = {0x82, 0x35, 0x7F};
        notes.observe(on);
        notes.observe(on);
        notes.observe(off);
        assert(notes.active_count() == 1);
        std::vector<std::tuple<int, int, int>> stops;
        const auto submitted = notes.stop_all([&](auto status, auto data1, auto data2) {
            stops.emplace_back(status, data1, data2);
        });
        assert(submitted == 1 && notes.active_count() == 0);
        assert((stops.front() == std::tuple<int, int, int>(0x82, 0x35, 0)));
    }
    {
        aitd4::ExplicitInitializer initializer;
        std::atomic_uint calls{};
        std::array<DWORD, 8> results{};
        std::array<std::thread, 8> threads;
        for (std::size_t index = 0; index != threads.size(); ++index) {
            threads[index] = std::thread([&, index] {
                results[index] = initializer.run([&] {
                    ++calls;
                    Sleep(10);
                    return true;
                });
            });
        }
        for (auto& thread : threads) thread.join();
        assert(calls == 1);
        for (const auto result : results) assert(result == 1);
        assert(initializer.run([&] { ++calls; return false; }) == 1);
        assert(calls == 1);
    }
    {
        aitd4::ExplicitInitializer initializer;
        unsigned calls = 0;
        assert(initializer.run([&] { ++calls; return false; }) == 0);
        assert(initializer.run([&] { ++calls; return true; }) == 0);
        assert(calls == 1);
    }
    return 0;
}
