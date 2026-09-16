#pragma once

#include <cstdint>

namespace aitd4 {

// Presentation frame pacing policy. The Windows layer owns the clock and the
// sleeping; this class owns the rules so the pacing decisions can be tested
// without a display, a game executable or a Bink handle.
class FrameLimiter {
public:
    // MovieLimit=Auto selects the rate declared by the playing movie instead of
    // a fixed value. This keeps retail encodes and replacement packs correct
    // even though they are authored at different frame rates.
    static constexpr std::int32_t automatic = -1;
    static constexpr std::int64_t default_movie_interval_us = 1000000 / 15;

    static std::int64_t fps_interval_us(int fps) {
        return fps > 0 ? 1000000 / fps : 0;
    }

    // Re-applies configuration only when it changed, so per-frame calls do not
    // discard the in-flight pacing deadline.
    void configure(bool enabled, int game_fps, std::int32_t movie_fps) {
        if (enabled_ == enabled && game_fps_ == game_fps && movie_fps_ == movie_fps) return;
        enabled_ = enabled;
        game_fps_ = game_fps;
        movie_fps_ = movie_fps;
        reset();
    }

    void reset() { deadline_us_ = 0; }

    bool enabled() const { return enabled_; }
    int game_fps() const { return game_fps_; }
    std::int32_t movie_setting() const { return movie_fps_; }

    // Target interval before the next presentation. authored_movie_interval_us
    // is the live BINK rate and is used only for the automatic movie setting; a
    // nonpositive value falls back to the documented retail rate.
    std::int64_t interval_us(bool movie_active,
                             std::int64_t authored_movie_interval_us) const {
        if (!enabled_) return 0;
        if (!movie_active) return fps_interval_us(game_fps_);
        if (movie_fps_ == automatic)
            return authored_movie_interval_us > 0 ? authored_movie_interval_us
                                                  : default_movie_interval_us;
        return fps_interval_us(movie_fps_);
    }

    // Updates the pacing deadline and reports how long the caller must wait.
    // Frames that arrive late resynchronize instead of bursting to catch up.
    std::int64_t wait_us(std::int64_t now_us, std::int64_t interval_us) {
        if (interval_us <= 0) {
            deadline_us_ = 0;
            return 0;
        }
        if (deadline_us_ == 0) {
            deadline_us_ = now_us + interval_us;
            return 0;
        }
        const std::int64_t remaining = deadline_us_ - now_us;
        if (remaining <= 0) {
            deadline_us_ = now_us + interval_us;
            return 0;
        }
        deadline_us_ += interval_us;
        return remaining;
    }

private:
    bool enabled_{};
    int game_fps_{};
    std::int32_t movie_fps_{automatic};
    std::int64_t deadline_us_{};
};

}  // namespace aitd4
