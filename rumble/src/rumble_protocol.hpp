#pragma once

// Platform-independent Dreamcast vibration decoding. Selector/value calls are
// reconstructed into pdVib commands and mapped to XInput motor strength here;
// timing, threads and executable hooks remain in the runtime fragments.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aitdtnn::rumble {

constexpr std::uint32_t kStopVibset = 0x00000010u;
constexpr std::uint32_t kWeakVibset = 0x00202011u;
constexpr std::uint32_t kStrongVibset = 0x00207011u;
constexpr std::uint32_t kDreamcastAutoStopMilliseconds = 5000u;

struct DecodedVibration {
    std::uint32_t raw_vibset{};
    float power{};
    float inclination{};
    std::uint32_t duration_milliseconds{};
};

inline std::uint32_t request_to_vibset(std::uint32_t selector,
                                       std::uint32_t value) noexcept {
    if (value == 0) return kStopVibset;
    return selector == 0 ? kStrongVibset : kWeakVibset;
}

inline DecodedVibration decode_vibset(std::uint32_t vibset,
                                      std::uint32_t auto_stop_milliseconds =
                                          kDreamcastAutoStopMilliseconds) noexcept {
    const std::uint32_t positive_power = (vibset >> 8u) & 7u;
    const std::uint32_t negative_power = (vibset >> 12u) & 7u;
    const std::uint32_t frequency = (vibset >> 16u) & 0xffu;
    std::int32_t increment = static_cast<std::int8_t>(vibset >> 24u);
    if ((vibset & 0x8000u) != 0) {
        increment = -increment;
    } else if ((vibset & 0x0800u) == 0) {
        increment = 0;
    }
    const bool continuous = (vibset & 1u) != 0;
    const std::uint32_t peak_power = std::max(positive_power, negative_power);
    const float power = std::min(
        static_cast<float>(positive_power + negative_power) / 7.0f, 1.0f);

    std::uint32_t duration = auto_stop_milliseconds;
    if (frequency > 0 && (!continuous || increment != 0)) {
        const std::uint32_t numerator = increment != 0
            ? static_cast<std::uint32_t>(std::abs(increment)) * peak_power
            : 1u;
        duration = std::min(1000u * numerator / frequency,
                            auto_stop_milliseconds);
    }
    float inclination = 0.0f;
    if (increment != 0 && power != 0.0f && peak_power != 0) {
        inclination = static_cast<float>(frequency) /
            (1000.0f * static_cast<float>(increment) *
             static_cast<float>(peak_power));
    }
    return {vibset, power, inclination, duration};
}

inline std::uint16_t to_xinput_motor(float power, float strength) noexcept {
    return static_cast<std::uint16_t>(std::clamp(
        power * strength * 65535.0f, 0.0f, 65535.0f));
}

}  // namespace aitdtnn::rumble
