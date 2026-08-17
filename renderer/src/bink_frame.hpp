#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace aitd4 {

constexpr std::uint32_t bink_surface_mask = 0x0fu;
constexpr std::uint32_t bink_surface_24r = 2u;

struct BinkFrameView {
    const std::uint8_t* pixels{};
    int pitch{};
    int width{};
    int height{};
    std::uint32_t flags{};
};

inline bool copy_bink_frame_to_rgba_canvas(const BinkFrameView& frame, int canvas_width,
                                           int canvas_height, int canvas_x, int canvas_y,
                                           std::vector<std::uint8_t>& rgba) {
    if (!frame.pixels || frame.width <= 0 || frame.height <= 0 || canvas_width <= 0 ||
        canvas_height <= 0 || canvas_x < 0 || canvas_y < 0 ||
        canvas_x + frame.width > canvas_width || canvas_y + frame.height > canvas_height ||
        (frame.flags & bink_surface_mask) != bink_surface_24r ||
        std::abs(frame.pitch) < frame.width * 3)
        return false;

    rgba.assign(static_cast<std::size_t>(canvas_width) * canvas_height * 4, 0);
    for (int source_y = 0; source_y < frame.height; ++source_y) {
        const auto* source = frame.pixels + static_cast<std::ptrdiff_t>(source_y) * frame.pitch;
        const int gl_y = canvas_height - 1 - (canvas_y + source_y);
        auto* destination = rgba.data() +
            (static_cast<std::size_t>(gl_y) * canvas_width + canvas_x) * 4;
        for (int x = 0; x < frame.width; ++x) {
            // BINKSURFACE24R is the red/blue-reversed 24-bit surface: bytes are RGB.
            destination[x * 4 + 0] = source[x * 3 + 0];
            destination[x * 4 + 1] = source[x * 3 + 1];
            destination[x * 4 + 2] = source[x * 3 + 2];
            destination[x * 4 + 3] = 255;
        }
    }
    return true;
}

}  // namespace aitd4
