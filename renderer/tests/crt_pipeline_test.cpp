#include "../src/bink_frame.hpp"
#include "../src/crt_math.hpp"
#include "../src/viewport.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

bool close(float left, float right, float tolerance) {
    return std::abs(left - right) <= tolerance;
}

}  // namespace

int main() {
    const auto viewport = aitd4::proportional_4x3_viewport(1920, 1080);
    if (viewport.x != 240 || viewport.y != 0 || viewport.width != 1440 ||
        viewport.height != 1080)
        return 1;

    if (!close(aitd4::signal_row(0.0f, 1080, 480), -0.2777778f, 0.00001f) ||
        !close(aitd4::signal_row(1079.0f, 1080, 480), 479.27778f, 0.0001f))
        return 2;

    for (int sample = 0; sample <= 255; ++sample) {
        const float encoded = sample / 255.0f;
        const float round_trip = aitd4::linear_to_srgb(aitd4::srgb_to_linear(encoded));
        if (!close(encoded, round_trip, 0.00001f)) return 3;
    }

    std::array<float, 3> mean{};
    for (int x = 0; x < 3; ++x) {
        const auto gain = aitd4::aperture_grille_gain(x, 0.20f);
        for (int channel = 0; channel < 3; ++channel) mean[channel] += gain[channel] / 3.0f;
    }
    if (!close(mean[0], 1.0f, 0.00001f) || !close(mean[1], 1.0f, 0.00001f) ||
        !close(mean[2], 1.0f, 0.00001f))
        return 4;

    constexpr int width = 640;
    constexpr int height = 320;
    constexpr int padded_pitch = width * 3 + 16;
    std::vector<std::uint8_t> source(static_cast<std::size_t>(padded_pitch) * height, 0);
    source[0] = 11;
    source[1] = 22;
    source[2] = 33;
    const std::size_t last = static_cast<std::size_t>(height - 1) * padded_pitch;
    source[last + 0] = 44;
    source[last + 1] = 55;
    source[last + 2] = 66;
    std::vector<std::uint8_t> canvas;
    if (!aitd4::copy_bink_frame_to_rgba_canvas(
            {source.data(), padded_pitch, width, height, aitd4::bink_surface_24r},
            640, 480, 0, 80, canvas))
        return 5;
    const auto pixel = [&](int x, int gl_y, int channel) -> std::uint8_t {
        return canvas[(static_cast<std::size_t>(gl_y) * 640 + x) * 4 + channel];
    };
    if (pixel(0, 399, 0) != 11 || pixel(0, 399, 1) != 22 ||
        pixel(0, 399, 2) != 33 || pixel(0, 80, 0) != 44 ||
        pixel(0, 80, 1) != 55 || pixel(0, 80, 2) != 66 ||
        pixel(0, 79, 0) != 0 || pixel(0, 400, 0) != 0)
        return 6;

    std::vector<std::uint8_t> negative_storage(static_cast<std::size_t>(width) * 3 * 2, 0);
    auto* negative_top = negative_storage.data() + width * 3;
    negative_top[0] = 70;
    negative_top[1] = 80;
    negative_top[2] = 90;
    negative_storage[0] = 100;
    negative_storage[1] = 110;
    negative_storage[2] = 120;
    if (!aitd4::copy_bink_frame_to_rgba_canvas(
            {negative_top, -width * 3, width, 2, aitd4::bink_surface_24r},
            640, 480, 0, 0, canvas) ||
        pixel(0, 479, 0) != 70 || pixel(0, 478, 0) != 100)
        return 7;

    if (aitd4::copy_bink_frame_to_rgba_canvas(
            {source.data(), padded_pitch, width, height, 1}, 640, 480, 0, 80, canvas))
        return 8;
    if (!aitd4::copy_bink_frame_to_rgba_canvas(
            {source.data(), padded_pitch, width, height, aitd4::bink_surface_24r},
            640, 480, 0, 80, canvas))
        return 9;

    std::puts("CRT math and Bink frame tests passed");
    return 0;
}
