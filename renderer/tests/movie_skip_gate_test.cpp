#include "../src/character_select_movie_gate.hpp"
#include "../src/movie_skip_gate.hpp"
#include "../src/cutscene_input_guard.hpp"

#include <cstdio>

int main() {
    aitd4::MovieSkipGate gate;
    constexpr std::uint32_t unrelated = 0x00000020u;

    gate.reset();
    if (gate.armed()) return 1;
    if (gate.filter(aitd4::MovieSkipGate::skip_mask | unrelated,
                    aitd4::MovieSkipGate::skip_mask) != unrelated)
        return 2;
    if (gate.armed()) return 3;
    if (gate.filter(aitd4::MovieSkipGate::skip_mask, 0) != 0 || !gate.armed()) return 4;
    if (gate.filter(aitd4::MovieSkipGate::skip_mask,
                    aitd4::MovieSkipGate::skip_mask) !=
        aitd4::MovieSkipGate::skip_mask)
        return 5;
    // A duplicate game-side new-press snapshot while still held is not a real
    // controller edge and must not skip.
    if (gate.filter(aitd4::MovieSkipGate::skip_mask,
                    aitd4::MovieSkipGate::skip_mask) != 0)
        return 6;
    if (gate.filter(0, 0) != 0) return 7;
    if (gate.filter(aitd4::MovieSkipGate::skip_mask,
                    aitd4::MovieSkipGate::skip_mask) !=
        aitd4::MovieSkipGate::skip_mask)
        return 8;

    gate.reset();
    if (gate.filter(aitd4::MovieSkipGate::skip_mask | unrelated, 0) != unrelated ||
        !gate.armed()) return 9;
    if (gate.filter(aitd4::MovieSkipGate::skip_mask | unrelated,
                    aitd4::MovieSkipGate::skip_mask) !=
        (aitd4::MovieSkipGate::skip_mask | unrelated))
        return 10;

    aitd4::CutsceneInputGuard transition_guard;
    if (transition_guard.waiting_for_neutral()) return 11;
    transition_guard.consume_after_character_selection();
    if (!transition_guard.waiting_for_neutral()) return 12;
    if (transition_guard.filter(aitd4::CutsceneInputGuard::action_mask | unrelated,
                                aitd4::CutsceneInputGuard::action_mask) != unrelated)
        return 13;
    // A synthetic re-press while the physical button remains held is consumed.
    if (transition_guard.filter(aitd4::CutsceneInputGuard::action_mask,
                                aitd4::CutsceneInputGuard::action_mask) != 0)
        return 14;
    if (transition_guard.filter(unrelated, 0) != unrelated ||
        transition_guard.waiting_for_neutral())
        return 15;
    if (transition_guard.filter(aitd4::CutsceneInputGuard::action_mask,
                                aitd4::CutsceneInputGuard::action_mask) !=
        aitd4::CutsceneInputGuard::action_mask)
        return 16;

    static_assert(aitd4::CharacterSelectMovieGate::movie_for_item(0x2001) == 5);
    static_assert(aitd4::CharacterSelectMovieGate::movie_for_item(0x2002) == 4);
    static_assert(aitd4::CharacterSelectMovieGate::movie_for_item(0x9999) == -1);
    static_assert(aitd4::CharacterSelectMovieGate::movie_for_character_byte(0) == 4);
    static_assert(aitd4::CharacterSelectMovieGate::movie_for_character_byte(1) == 5);
    static_assert(aitd4::CharacterSelectMovieGate::movie_for_character_byte(0xFF) == 5);

    std::puts("cutscene input reuse and movie skip release-gate tests passed");
    return 0;
}
