#pragma once

#include <cstdint>

namespace aitd4 {

// The PC input snapshot can be observed by more than one frontend/gameplay
// consumer during a transition.  Character selection must receive the action
// edge, but the same edge must not be re-used by the level script that follows.
class CutsceneInputGuard {
public:
    static constexpr std::uint32_t action_mask = 0x40800000u;

    void consume_after_character_selection() { waiting_for_neutral_ = true; }

    std::uint32_t filter(std::uint32_t new_press, std::uint32_t held) {
        if (!waiting_for_neutral_) return new_press;

        const std::uint32_t action_state = (new_press | held) & action_mask;
        if (action_state == 0) {
            waiting_for_neutral_ = false;
            return new_press;
        }
        return new_press & ~action_mask;
    }

    bool waiting_for_neutral() const { return waiting_for_neutral_; }

private:
    bool waiting_for_neutral_{};
};

}  // namespace aitd4
