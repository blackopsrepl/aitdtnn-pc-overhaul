#pragma once

// Pure state rules for one persistent game music player. A sequence pointer can
// survive while a room reloads its shared bank, so bank generation is part of
// binding identity. Keeping the rule free of Windows code makes it testable.

#include <array>
#include <cstdint>

namespace aitd4 {

class MidiNoteState {
public:
    void observe(const std::uint8_t* message) {
        if (!message) return;
        const auto kind = message[0] & 0xF0;
        const auto channel = message[0] & 0x0F;
        const auto note = message[1] & 0x7F;
        auto& count = active_[channel][note];
        if (kind == 0x90 && message[2] != 0) {
            if (count != 0xFF) ++count;
        } else if (kind == 0x80 || (kind == 0x90 && message[2] == 0)) {
            if (count != 0) --count;
        }
    }

    template <typename Submit>
    unsigned stop_all(Submit&& submit) {
        unsigned submitted = 0;
        for (unsigned channel = 0; channel != active_.size(); ++channel) {
            for (unsigned note = 0; note != active_[channel].size(); ++note) {
                auto& count = active_[channel][note];
                while (count != 0) {
                    submit(static_cast<std::uint8_t>(0x80 | channel),
                           static_cast<std::uint8_t>(note), std::uint8_t{0});
                    --count;
                    ++submitted;
                }
            }
        }
        return submitted;
    }

    unsigned active_count() const {
        unsigned result = 0;
        for (const auto& channel : active_)
            for (const auto count : channel) result += count;
        return result;
    }

private:
    std::array<std::array<std::uint8_t, 128>, 16> active_{};
};

} // namespace aitd4
