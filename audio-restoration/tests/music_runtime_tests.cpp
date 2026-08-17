#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "audio_hook_api.hpp"
#include "explicit_initializer.hpp"
#include "midi_lifecycle.hpp"
#include "music_identity.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

using aitd4::music_identity::Candidate;
using aitd4::music_identity::Evidence;

using AudioInitializeAbi = DWORD (WINAPI*)(void*);
static_assert(std::is_same_v<decltype(&AITD4_AudioInitialize), AudioInitializeAbi>);

int main() {
    {
        const std::array candidates{Candidate{"jardin2", false, 0}};
        const auto result = aitd4::music_identity::resolve(candidates);
        assert(result.index == 0 && result.evidence == Evidence::map_unique);
    }
    {
        const std::array candidates{
            Candidate{"jardinc0", true, 0}, Candidate{"jardinv0", false, 0}};
        const auto result = aitd4::music_identity::resolve(candidates);
        assert(result.index == 0 && result.evidence == Evidence::sequence_unique);
    }
    {
        // A stale/preloaded file must not override a unique live DSEQ match.
        const std::array candidates{
            Candidate{"jardinc0", true, 11}, Candidate{"jardinv0", false, 12}};
        const auto result = aitd4::music_identity::resolve(candidates);
        assert(result.index == 0 && result.evidence == Evidence::sequence_unique);
    }
    {
        // These retail containers have identical maps and sequence payloads.
        // Only the file actually requested by the game can resolve the pair.
        const std::array candidates{
            Candidate{"act_c12", true, 41}, Candidate{"act_c13", true, 42}};
        const auto result = aitd4::music_identity::resolve(candidates);
        assert(result.index == 1 && result.evidence == Evidence::file_load);
    }
    {
        const std::array candidates{
            Candidate{"inv_b1", true, 0}, Candidate{"jarding1", true, 0}};
        const auto result = aitd4::music_identity::resolve(candidates);
        assert(result.index == -1 && result.evidence == Evidence::ambiguous);
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
