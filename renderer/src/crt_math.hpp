#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace aitd4 {

inline float srgb_to_linear(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.04045f ? value / 12.92f
                             : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

inline float linear_to_srgb(float value) {
    value = std::max(value, 0.0f);
    return value <= 0.0031308f ? value * 12.92f
                              : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

inline std::array<float, 3> aperture_grille_gain(int physical_x, float strength) {
    strength = std::clamp(strength, 0.0f, 1.0f);
    std::array<float, 3> gain{1.0f - strength, 1.0f - strength, 1.0f - strength};
    gain[static_cast<unsigned>(physical_x % 3 + 3) % 3] = 1.0f + 2.0f * strength;
    return gain;
}

inline float signal_row(float output_y, int output_height, int signal_height) {
    if (output_height <= 0 || signal_height <= 0) return 0.0f;
    return (output_y + 0.5f) * static_cast<float>(signal_height) /
           static_cast<float>(output_height) - 0.5f;
}

}  // namespace aitd4
