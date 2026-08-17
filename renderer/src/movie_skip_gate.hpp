#pragma once

#include <cstdint>

namespace aitd4 {

class MovieSkipGate {
public:
    static constexpr std::uint32_t skip_mask = 0x40800000u;

    void reset() {
        armed_ = false;
        previous_held_ = skip_mask;
    }

    std::uint32_t filter(std::uint32_t new_press, std::uint32_t held) {
        const std::uint32_t held_skip = held & skip_mask;
        if (!armed_) {
            if (held_skip == 0) {
                armed_ = true;
                previous_held_ = 0;
            } else {
                previous_held_ = held_skip;
            }
            // A movie must observe a neutral controller sample before it becomes
            // skippable. This consumes a carried A/Space/Enter state at startup.
            return new_press & ~skip_mask;
        }

        // Do not trust the game's new-press snapshot by itself: reacquisition can
        // synthesize another edge while the button is continuously held. Permit
        // only bits that also have a real held-state 0 -> 1 transition observed by
        // this gate after the movie opened.
        const std::uint32_t rising = held_skip & ~previous_held_;
        previous_held_ = held_skip;
        return (new_press & ~skip_mask) | (new_press & skip_mask & rising);
    }

    bool armed() const { return armed_; }

private:
    bool armed_{};
    std::uint32_t previous_held_{skip_mask};
};

}  // namespace aitd4
