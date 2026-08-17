#pragma once

#include <cstdint>

namespace aitd4 {

class CharacterSelectMovieGate {
public:
    static constexpr std::uint32_t carnby_item = 0x2001u;
    static constexpr std::uint32_t aline_item = 0x2002u;
    static constexpr int select_aline_movie = 4;
    static constexpr int select_carnby_movie = 5;

    static constexpr int movie_for_item(std::uint32_t item) {
        if (item == carnby_item) return select_carnby_movie;
        if (item == aline_item) return select_aline_movie;
        return -1;
    }

    // The completed new-game controller writes 0 for Aline and a nonzero value
    // for Carnby before reaching the Dreamcast-authored movie insertion point.
    static constexpr int movie_for_character_byte(std::uint8_t character) {
        return character ? select_carnby_movie : select_aline_movie;
    }
};

}  // namespace aitd4
