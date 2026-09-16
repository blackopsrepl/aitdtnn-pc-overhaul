// Presentation frame pacing and authored movie-rate detection.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.
//
// The game drives gameplay logic from frame count, so pacing the present caps both
// the displayed rate and the simulation rate. Cinematics are paced to the rate the
// playing Bink file declares, which keeps retail encodes and replacement packs
// correct without hard-coding one frame rate for all content.

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

FrameLimiter frame_limiter;
std::int64_t qpc_frequency{};
std::int64_t qpc_epoch{};
HANDLE pacing_timer{};
int display_refresh_hz{};
std::uint32_t frame_limited_presents{};
std::int64_t last_movie_interval_us{};
void* last_logged_pacing_movie{};

constexpr std::int64_t spin_tail_us = 400;

std::int64_t query_us() {
    if (qpc_frequency <= 0) return 0;
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const std::int64_t delta = counter.QuadPart - qpc_epoch;
    if (delta <= 0) return 0;
    return (delta / qpc_frequency) * 1000000 +
           ((delta % qpc_frequency) * 1000000) / qpc_frequency;
}

int query_display_refresh() {
    DEVMODEA mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &mode) &&
        mode.dmDisplayFrequency > 1)
        return static_cast<int>(mode.dmDisplayFrequency);
    return 0;
}

// Reads the FrameRate/FrameRateDiv pair from the live Bink handle. Two candidate
// field layouts are accepted because Bink 1.x revisions differ; an implausible
// pair is ignored so an unknown file falls back to the documented default rate.
std::int64_t bink_frame_interval_us(const void* movie) {
    if (!movie || IsBadReadPtr(movie, 0x1C)) return 0;
    const auto* bytes = static_cast<const std::uint8_t*>(movie);
    constexpr std::size_t rate_offsets[]{0x14, 0x10};
    constexpr std::size_t divisor_offsets[]{0x18, 0x14};
    for (int candidate = 0; candidate < 2; ++candidate) {
        std::uint32_t rate = 0;
        std::uint32_t divisor = 0;
        std::memcpy(&rate, bytes + rate_offsets[candidate], sizeof(rate));
        std::memcpy(&divisor, bytes + divisor_offsets[candidate], sizeof(divisor));
        if (rate == 0 || divisor == 0 || divisor > rate) continue;
        const std::uint32_t fps = rate / divisor;
        if (fps < 5 || fps > 120) continue;
        return 1000000LL * static_cast<std::int64_t>(divisor) / rate;
    }
    return 0;
}

void wait_microseconds(std::int64_t microseconds) {
    if (microseconds <= 0) return;
    if (qpc_frequency <= 0) {
        Sleep(static_cast<DWORD>(microseconds / 1000));
        return;
    }
    const std::int64_t target = query_us() + microseconds;
    for (;;) {
        const std::int64_t remaining = target - query_us();
        if (remaining <= 0) return;
        if (remaining <= spin_tail_us + 500) {
            YieldProcessor();
            continue;
        }
        const std::int64_t sleep_us = remaining - spin_tail_us;
        if (pacing_timer) {
            LARGE_INTEGER due{};
            due.QuadPart = -sleep_us * 10;
            if (SetWaitableTimer(pacing_timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(pacing_timer, INFINITE);
                continue;
            }
        }
        const DWORD milliseconds = static_cast<DWORD>(sleep_us / 1000);
        Sleep(milliseconds ? milliseconds : 1);
    }
}

// Diagnostic only: records whether the game itself manages the swap interval.
// The renderer's own swap-interval call does not pass through this path.
BOOL WINAPI hooked_game_swap_interval(int value) {
    game_requested_swap_interval = value;
    log_line("game requested swap interval=%d", value);
    return real_game_swap_interval ? real_game_swap_interval(value) : FALSE;
}

void initialize_frame_limiter() {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER counter{};
    if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 &&
        QueryPerformanceCounter(&counter)) {
        qpc_frequency = frequency.QuadPart;
        qpc_epoch = counter.QuadPart;
    }
    display_refresh_hz = query_display_refresh();
    pacing_timer = CreateWaitableTimerExW(nullptr, nullptr,
                                          CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
    if (!pacing_timer) pacing_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    log_line("frame pacing initialized qpc_hz=%lld refresh=%d timer=%s",
             static_cast<long long>(qpc_frequency), display_refresh_hz,
             pacing_timer ? "high-resolution" : "sleep");
}

std::int64_t authored_movie_interval() {
    if (!active_bink_movie) return 0;
    const std::int64_t interval = bink_frame_interval_us(active_bink_movie);
    if (active_bink_movie != last_logged_pacing_movie) {
        last_logged_pacing_movie = active_bink_movie;
        last_movie_interval_us = interval;
        log_line("frame pacing movie=%p authored_interval_us=%lld setting=%d",
                 active_bink_movie, static_cast<long long>(interval),
                 g_config.frame_limit_movie);
    }
    return interval;
}

// Waits out the remainder of the target interval. The cap is enforced by this
// timer rather than by VSync: the driver and monitor decide whether and how the
// swap is synchronized, so the limiter must own the frame rate to guarantee the
// configured maximum on every display.
void pace_interval(std::int64_t interval) {
    if (interval <= 0) {
        frame_limiter.reset();
        return;
    }
    const std::int64_t wait = frame_limiter.wait_us(query_us(), interval);
    if (wait > 0) {
        ++frame_limited_presents;
        wait_microseconds(wait);
    }
}

// Movie frames advance through BinkDoFrame for both the native OpenGL and the
// alternate presentation-buffer backend, so pacing there limits the cinematic
// rate regardless of which backend presents.
void pace_movie_frame(void* bink) {
    if (!g_config.frame_limit_enabled || bink == nullptr || bink != active_bink_movie) return;
    frame_limiter.configure(true, g_config.frame_limit_game, g_config.frame_limit_movie);
    pace_interval(frame_limiter.interval_us(true, authored_movie_interval()));
}

// Called immediately before each real SwapBuffers. Gameplay is paced here; a
// playing movie was already paced at its frame advance, so it is not delayed a
// second time per present. VSync is never modified: it is left to synchronize
// the in-viewport Bink movie presentation.
void pace_present() {
    const bool enabled = g_config.frame_limit_enabled;
    frame_limiter.configure(enabled, g_config.frame_limit_game, g_config.frame_limit_movie);
    if (!enabled) {
        frame_limiter.reset();
        return;
    }
    if (active_bink_movie) return;
    pace_interval(frame_limiter.interval_us(false, 0));
}
