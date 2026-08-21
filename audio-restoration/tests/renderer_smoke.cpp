#include "audio_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (!aitd4::renderer_start(argv[1], nullptr) ||
        !aitd4::renderer_set_scene(0, "gamesnd", 5)) return 3;
    const std::uint8_t program[]{0xC0, 0x00, 0x00};
    const std::uint8_t note[]{0x90, 0x3C, 0x7F};
    aitd4::renderer_event(0, "gamesnd", 0, program);
    aitd4::renderer_event(0, "gamesnd", 0, note);
    std::array<std::int16_t, 2048 * 2> pcm{};
    unsigned peak = 0;
    std::uint64_t nonzero = 0;
    for (unsigned block = 0; block != 32; ++block) {
        if (!aitd4::renderer_render_pcm(pcm.data(), 2048)) return 4;
        for (const auto value : pcm) {
            peak = (std::max)(peak, value == INT16_MIN ? 32768u :
                              static_cast<unsigned>(std::abs(static_cast<int>(value))));
            nonzero += value != 0;
        }
    }
    std::printf("AICA smoke: frames=%u nonzero=%llu peak=%u\n", 2048u * 32,
                static_cast<unsigned long long>(nonzero), peak);
    return peak && nonzero ? 0 : 5;
}
